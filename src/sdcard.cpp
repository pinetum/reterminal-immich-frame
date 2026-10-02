#include "sdcard.h"
#include "pins.h"
#include "log.h"
#include "display.h"

#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <new>

static bool s_ready = false;

bool sdCardPresent() {
  pinMode(PIN_SD_DET, INPUT_PULLUP);
  return digitalRead(PIN_SD_DET) == LOW;   // LOW = card inserted
}

bool sdBegin() {
  if (s_ready) return true;

  // Power the slot first and give it a moment to come up.
  pinMode(PIN_SD_EN, OUTPUT);
  digitalWrite(PIN_SD_EN, HIGH);
  pinMode(PIN_SD_DET, INPUT_PULLUP);
  delay(50);

  if (!sdCardPresent())
    LOG.println("[sd] SD_DET says no card (pin may also be floating) -- trying anyway");

  // The card and the panel share SCK/MISO/MOSI (7/8/9) with separate chip
  // selects. Rather than creating a second SPIClass that would fight the panel
  // driver for the bus, reuse the exact instance Seeed_GFX set up. This is the
  // sequence the official Seeed E1004 example uses; deviating from it is the
  // usual reason SD.begin() fails on this board.
  SPIClass& spi = displaySpi();
  spi.end();
  spi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, /*ss=*/-1);

  if (!SD.begin(PIN_SD_CS, spi)) {
    LOG.println("[sd] SD.begin FAILED -- card inserted? formatted FAT32? (<=64GB)");
    return false;
  }
  s_ready = true;
  LOG.printf("[sd] mounted, %llu MB\n", (unsigned long long)sdSizeMB());

  if (!SD.exists(SD_DIR_ROOT))  SD.mkdir(SD_DIR_ROOT);
  if (!SD.exists(SD_DIR_CACHE)) SD.mkdir(SD_DIR_CACHE);
  return true;
}

bool sdReady() { return s_ready; }

uint64_t sdSizeMB() { return s_ready ? SD.cardSize() / (1024ULL * 1024ULL) : 0; }

// ---------------------------------------------------------------------------
// Frame cache
// ---------------------------------------------------------------------------
static String cachePath(const String& assetId) {
  return String(SD_DIR_CACHE) + "/" + assetId + ".e6";
}

bool cacheHas(const String& assetId) {
  if (!s_ready || assetId.isEmpty()) return false;
  const String p = cachePath(assetId);
  if (!SD.exists(p)) return false;
  // A truncated file (power lost mid-write) must not be treated as a hit.
  File f = SD.open(p, FILE_READ);
  if (!f) return false;
  const bool ok = (f.size() == PANEL_FRAME_BYTES);
  f.close();
  if (!ok) { LOG.printf("[cache] %s is truncated -- dropping\n", assetId.c_str()); SD.remove(p); }
  return ok;
}

bool cacheLoadFrame(const String& assetId, uint8_t* frame) {
  if (!s_ready || !frame) return false;
  File f = SD.open(cachePath(assetId), FILE_READ);
  if (!f) return false;
  if (f.size() != PANEL_FRAME_BYTES) { f.close(); return false; }
  const uint32_t t0 = millis();
  size_t got = 0;
  while (got < PANEL_FRAME_BYTES) {
    const int n = f.read(frame + got, PANEL_FRAME_BYTES - got);
    if (n <= 0) break;
    got += n;
  }
  f.close();
  if (got != PANEL_FRAME_BYTES) { LOG.println("[cache] short read"); return false; }
  LOG.printf("[cache] HIT %s (%lu ms)\n", assetId.c_str(), (unsigned long)(millis() - t0));
  return true;
}

bool cacheStoreFrame(const String& assetId, const uint8_t* frame) {
  if (!s_ready || !frame || assetId.isEmpty()) return false;
  const String p = cachePath(assetId);
  const String tmp = p + ".part";
  File f = SD.open(tmp, FILE_WRITE);
  if (!f) { LOG.println("[cache] cannot open for write"); return false; }
  size_t put = 0;
  while (put < PANEL_FRAME_BYTES) {
    // 16 kB chunks: large enough to keep the SD happy, small enough that a
    // failure does not strand a huge write.
    const size_t n = min((size_t)16384, PANEL_FRAME_BYTES - put);
    const size_t w = f.write(frame + put, n);
    if (w != n) { f.close(); SD.remove(tmp); LOG.println("[cache] write failed"); return false; }
    put += n;
  }
  f.close();
  // Rename only once the bytes are all down, so a hit is always a whole frame.
  SD.remove(p);
  if (!SD.rename(tmp, p)) { SD.remove(tmp); return false; }
  LOG.printf("[cache] stored %s\n", assetId.c_str());
  return true;
}

// Enumerating the cache directory three slightly different ways was asking for
// one of them to drift, so all of them go through this. Names are collected on
// the heap before anything is deleted: removing entries while iterating a FAT
// directory is unreliable, and a 256-entry String array on the 8 kB task stack
// is not an option either.
// File::name() returns a bare basename on ESP32 Arduino core 3.x but returned a
// full path on core 2.x. Handle both so the project is not pinned to one core.
static String cacheEntryPath(const String& name) {
  return name.startsWith("/") ? name : (String(SD_DIR_CACHE) + "/" + name);
}

static int cacheList(String** out, int max) {
  *out = nullptr;
  if (!s_ready) return 0;
  File dir = SD.open(SD_DIR_CACHE);
  if (!dir) return 0;

  String* names = new (std::nothrow) String[max];
  if (!names) { dir.close(); return 0; }

  int n = 0;
  for (;;) {
    File e = dir.openNextFile();
    if (!e) break;
    const String nm = String(e.name());
    e.close();
    // Skip directories and half-written ".part" files -- only finished frames
    // count towards the cache size or are eligible for eviction.
    if (nm.endsWith(".e6") && n < max) names[n++] = nm;   // basename or full path
    if (n >= max) break;
  }
  dir.close();
  *out = names;
  return n;
}

// How many entries the pruner will consider in one pass. Well above any sane
// cacheFrames setting, and bounded so the allocation stays small.
static const int kCacheScanMax = 256;

void cacheTouch(const String& assetId) {
  // The Arduino SD wrapper exposes no utime(), so "recently used" cannot be
  // recorded. Pruning therefore evicts in directory order, which on FAT is close
  // to insertion order. That is the right behaviour anyway for a slideshow that
  // walks the album in sequence.
  (void)assetId;
}

uint32_t cacheCount() {
  String* names = nullptr;
  const int n = cacheList(&names, kCacheScanMax);
  delete[] names;
  return (uint32_t)n;
}

void cachePrune(uint32_t keep) {
  String* names = nullptr;
  const int n = cacheList(&names, kCacheScanMax);
  if (!names) return;
  if ((uint32_t)n > keep) {
    const int toDrop = n - (int)keep;
    for (int i = 0; i < toDrop; ++i) {
      if (SD.remove(cacheEntryPath(names[i])))
        LOG.printf("[cache] evicted %s\n", names[i].c_str());
    }
  }
  delete[] names;
}

void cacheClear() {
  String* names = nullptr;
  const int n = cacheList(&names, kCacheScanMax);
  if (!names) return;
  for (int i = 0; i < n; ++i) SD.remove(cacheEntryPath(names[i]));
  delete[] names;
  LOG.printf("[cache] cleared %d frames\n", n);
}
