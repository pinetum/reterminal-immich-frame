#include "playlist.h"
#include "sdcard.h"
#include "settings.h"
#include "log.h"

#include <SD.h>
#include <esp_random.h>

// ---- state that must outlive deep sleep ------------------------------------
RTC_DATA_ATTR static uint32_t s_rtcMagic   = 0;
RTC_DATA_ATTR static int32_t  s_cursor     = 0;
RTC_DATA_ATTR static uint32_t s_count      = 0;
RTC_DATA_ATTR static uint32_t s_ageMinutes = 0xFFFFFFFF;  // "unknown" => stale
RTC_DATA_ATTR static uint32_t s_shuffleSeed = 0;
RTC_DATA_ATTR static uint32_t s_albumHash  = 0;

static const uint32_t kMagic = 0x494D4331;  // "IMC1"

static uint32_t hashString(const String& s) {
  uint32_t h = 2166136261u;             // FNV-1a
  for (size_t i = 0; i < s.length(); ++i) { h ^= (uint8_t)s[i]; h *= 16777619u; }
  return h;
}

static uint32_t countRecords() {
  if (!sdReady()) return 0;
  File f = SD.open(SD_PATH_PLAYLIST, FILE_READ);
  if (!f) return 0;
  const uint32_t n = (uint32_t)(f.size() / PLAYLIST_REC);
  f.close();
  return n;
}

// Pick a stride that is coprime with n, so repeatedly adding it visits every
// index exactly once. That gives a shuffle with O(1) next AND previous, which a
// Fisher-Yates permutation could not do without keeping the whole order in RAM.
static uint32_t coprimeStride(uint32_t n, uint32_t seed) {
  if (n < 3) return 1;
  uint32_t s = (seed % (n - 1)) + 1;
  for (uint32_t tries = 0; tries < n; ++tries) {
    uint32_t a = s, b = n;
    while (b) { const uint32_t t = a % b; a = b; b = t; }
    if (a == 1) return s;
    s = (s % (n - 1)) + 1;
  }
  return 1;
}

void playlistInit() {
  const uint32_t albumHash = hashString(g_cfg.albumId);

  if (s_rtcMagic != kMagic) {
    // Cold boot (or first ever). Recover what we can from the card.
    s_rtcMagic = kMagic;
    s_cursor = 0;
    s_count = 0;
    s_ageMinutes = 0xFFFFFFFF;
    s_shuffleSeed = 0;
    s_albumHash = albumHash;

    if (sdReady()) {
      File f = SD.open(SD_PATH_STATE, FILE_READ);
      if (f) {
        s_cursor      = f.readStringUntil('\n').toInt();
        s_ageMinutes  = (uint32_t)f.readStringUntil('\n').toInt();
        s_shuffleSeed = (uint32_t)f.readStringUntil('\n').toInt();
        s_albumHash   = (uint32_t)f.readStringUntil('\n').toInt();
        f.close();
        LOG.printf("[pl] restored state from SD: cursor=%ld age=%umin\n",
                   (long)s_cursor, (unsigned)s_ageMinutes);
      }
    }
  }

  if (s_albumHash != albumHash) {
    LOG.println("[pl] album changed -- playlist is stale and cursor resets");
    s_albumHash = albumHash;
    s_cursor = 0;
    s_ageMinutes = 0xFFFFFFFF;
  }

  s_count = countRecords();
  if (s_shuffleSeed == 0) s_shuffleSeed = esp_random() | 1u;
  LOG.printf("[pl] %u photos, cursor=%ld, age=%umin\n",
             (unsigned)s_count, (long)s_cursor, (unsigned)s_ageMinutes);
}

uint32_t playlistCount() { return s_count; }

bool playlistStale() {
  if (s_count == 0) return true;
  if (s_ageMinutes == 0xFFFFFFFF) return true;
  return s_ageMinutes >= g_cfg.playlistTtlHours * 60;
}

void playlistMarkFresh(uint32_t count) {
  s_count = count;
  s_ageMinutes = 0;
  // A fresh playlist gets a fresh shuffle order, so the sequence does not repeat
  // identically every time the album is re-read.
  s_shuffleSeed = esp_random() | 1u;
  if (s_count && s_cursor >= (int32_t)s_count) s_cursor = 0;
  playlistSave();
}

void playlistAgeBy(uint32_t minutes) {
  if (s_ageMinutes != 0xFFFFFFFF) s_ageMinutes += minutes;
}

uint32_t playlistAgeMinutes() { return s_ageMinutes; }

int playlistCursor() { return (int)s_cursor; }

void playlistSetCursor(int cursor) {
  if (s_count == 0) { s_cursor = 0; return; }
  s_cursor = ((cursor % (int32_t)s_count) + (int32_t)s_count) % (int32_t)s_count;
}

void playlistAdvance(int delta) { playlistSetCursor((int)s_cursor + delta); }

String playlistCurrentAsset() {
  if (!sdReady() || s_count == 0) return String();

  uint32_t idx = (uint32_t)s_cursor % s_count;
  if (g_cfg.shuffle) {
    const uint32_t step = coprimeStride(s_count, s_shuffleSeed);
    idx = (uint32_t)(((uint64_t)s_shuffleSeed % s_count + (uint64_t)idx * step) % s_count);
  }

  File f = SD.open(SD_PATH_PLAYLIST, FILE_READ);
  if (!f) return String();
  if (!f.seek((uint32_t)idx * PLAYLIST_REC)) { f.close(); return String(); }
  char buf[PLAYLIST_ID_LEN + 1] = {0};
  const int n = f.read((uint8_t*)buf, PLAYLIST_ID_LEN);
  f.close();
  if (n != PLAYLIST_ID_LEN) return String();
  String id(buf);
  id.trim();
  return id;
}

int playlistPosition() { return s_count ? (int)(s_cursor % s_count) + 1 : 0; }

void playlistSave() {
  if (!sdReady()) return;
  File f = SD.open(SD_PATH_STATE, FILE_WRITE);
  if (!f) return;
  f.printf("%ld\n%u\n%u\n%u\n", (long)s_cursor, (unsigned)s_ageMinutes,
           (unsigned)s_shuffleSeed, (unsigned)s_albumHash);
  f.close();
}
