#include "immich.h"
#include "settings.h"
#include "sdcard.h"
#include "log.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SD.h>
#include <ArduinoJson.h>

namespace {

// Keep JSON documents out of the 320 kB internal heap -- there is plenty of
// PSRAM and the internal heap is needed by Wi-Fi and TLS.
struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override {
    void* p = ps_malloc(n);
    return p ? p : malloc(n);
  }
  void deallocate(void* p) override { free(p); }
  void* reallocate(void* p, size_t n) override {
    void* q = ps_realloc(p, n);
    return q ? q : realloc(p, n);
  }
};
PsramAllocator g_alloc;

// Normalise whatever the user typed into a clean origin: no trailing slash and
// no trailing "/api" (pasting the URL straight out of the Immich API docs is a
// very easy mistake to make).
String baseUrl() {
  String u = g_cfg.immichUrl;
  u.trim();
  while (u.endsWith("/")) u.remove(u.length() - 1);
  if (u.endsWith("/api")) u.remove(u.length() - 4);
  while (u.endsWith("/")) u.remove(u.length() - 1);
  if (u.length() && !u.startsWith("http://") && !u.startsWith("https://")) u = "http://" + u;
  return u;
}

bool isHttps() { return baseUrl().startsWith("https://"); }

// HTTPClient needs the underlying client to stay alive for the whole request,
// so the two are bundled together.
struct Request {
  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;

  bool begin(const String& url) {
    if (isHttps()) {
      if (g_cfg.caPem.length() > 0) {
        secure.setCACert(g_cfg.caPem.c_str());
      } else {
        // Self-signed certificates are the norm for a home Immich install, and
        // there is no trust store on the device. Documented in the README as a
        // deliberate trade-off; paste a CA PEM in the admin page to enable
        // proper verification.
        secure.setInsecure();
      }
      if (!http.begin(secure, url)) return false;
    } else {
      if (!http.begin(plain, url)) return false;
    }
    http.setConnectTimeout(10000);
    http.setTimeout(20000);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("x-api-key", g_cfg.immichKey);
    http.addHeader("Accept", "application/json");
    return true;
  }
  ~Request() { http.end(); }
};

String httpErr(int code, HTTPClient& h) {
  if (code <= 0) return String("network error: ") + HTTPClient::errorToString(code);
  if (code == 401 || code == 403) return "HTTP " + String(code) + " - API key rejected";
  if (code == 404) return "HTTP 404 - not found (wrong album ID or server URL?)";
  return "HTTP " + String(code);
}

}  // namespace

bool immichListAlbums(String& jsonOut, String& err) {
  if (baseUrl().isEmpty() || g_cfg.immichKey.isEmpty()) { err = "server URL / API key not set"; return false; }

  Request r;
  const String url = baseUrl() + "/api/albums";
  if (!r.begin(url)) { err = "cannot open connection"; return false; }

  const int code = r.http.GET();
  if (code != 200) { err = httpErr(code, r.http); LOG.printf("[immich] albums: %s\n", err.c_str()); return false; }

  // Only three fields matter; the filter keeps a library with hundreds of albums
  // from ballooning the document.
  JsonDocument filter(&g_alloc);
  filter[0]["id"] = true;
  filter[0]["albumName"] = true;
  filter[0]["assetCount"] = true;

  JsonDocument doc(&g_alloc);
  const DeserializationError e =
      deserializeJson(doc, r.http.getStream(), DeserializationOption::Filter(filter));
  if (e) { err = String("bad JSON: ") + e.c_str(); return false; }

  // Re-serialise with ArduinoJson rather than concatenating strings: album names
  // are user data and may contain quotes, backslashes or control characters that
  // hand-rolled escaping would get wrong.
  JsonDocument slim(&g_alloc);
  JsonArray arr = slim.to<JsonArray>();
  for (JsonObject a : doc.as<JsonArray>()) {
    JsonObject o = arr.add<JsonObject>();
    o["id"]    = a["id"] | "";
    o["name"]  = a["albumName"] | "";
    o["count"] = (int)(a["assetCount"] | 0);
  }
  jsonOut = "";
  serializeJson(slim, jsonOut);
  return true;
}

bool immichTestConnection(String& err) {
  String json;
  return immichListAlbums(json, err);
}

int immichFetchPlaylist(String& err) {
  if (!g_cfg.immichReady()) { err = "server URL, API key or album not set"; return -1; }
  if (!sdReady()) { err = "no SD card"; return -1; }

  const String tmp = String(SD_PATH_PLAYLIST) + ".part";
  SD.remove(tmp);
  File out = SD.open(tmp, FILE_WRITE);
  if (!out) { err = "cannot write to SD"; return -1; }

  const int kPageSize = 500;
  const int kMaxAssets = 10000;
  int total = 0;

  for (int page = 1; page <= (kMaxAssets / kPageSize) + 1; ++page) {
    Request r;
    if (!r.begin(baseUrl() + "/api/search/metadata")) {
      err = "cannot open connection"; out.close(); SD.remove(tmp); return -1;
    }
    r.http.addHeader("Content-Type", "application/json");

    // NOTE on API compatibility: albumIds / page / type / order are marked
    // deprecated from Immich v3.2 onwards, but they are still accepted and they
    // are the only formulation that works on BOTH v1.9x and v3.x servers.
    // `GET /api/albums/{id}` is not an option: newer AlbumResponseDto no longer
    // carries an `assets` array at all, only assetCount. If a future Immich
    // removes these fields, switch to the cursor-based pagination
    // (query.cursor / nextCursor).
    String body = "{\"albumIds\":[\"" + g_cfg.albumId + "\"],\"type\":\"IMAGE\",\"size\":" +
                  String(kPageSize) + ",\"page\":" + String(page) + ",\"order\":\"asc\"}";

    const int code = r.http.POST(body);
    if (code != 200) {
      err = httpErr(code, r.http);
      LOG.printf("[immich] search: %s\n", err.c_str());
      out.close(); SD.remove(tmp);
      return -1;
    }

    JsonDocument filter(&g_alloc);
    filter["assets"]["items"][0]["id"] = true;
    filter["assets"]["count"] = true;
    filter["assets"]["total"] = true;

    JsonDocument doc(&g_alloc);
    const DeserializationError e =
        deserializeJson(doc, r.http.getStream(), DeserializationOption::Filter(filter));
    if (e) { err = String("bad JSON: ") + e.c_str(); out.close(); SD.remove(tmp); return -1; }

    JsonArray items = doc["assets"]["items"].as<JsonArray>();
    const int got = items.size();
    for (JsonObject a : items) {
      const char* id = a["id"];
      if (!id || !*id) continue;
      // Fixed-width record so playlist lookup stays a seek. Immich IDs are
      // UUIDv4 (exactly 36 chars); anything else is padded or truncated.
      char rec[PLAYLIST_REC];
      memset(rec, ' ', PLAYLIST_ID_LEN);
      rec[PLAYLIST_ID_LEN] = '\n';
      size_t n = strlen(id);
      if (n > PLAYLIST_ID_LEN) n = PLAYLIST_ID_LEN;
      memcpy(rec, id, n);
      if (out.write((const uint8_t*)rec, PLAYLIST_REC) != PLAYLIST_REC) {
        err = "SD write failed"; out.close(); SD.remove(tmp); return -1;
      }
      ++total;
    }
    LOG.printf("[immich] page %d: %d assets (running total %d)\n", page, got, total);

    if (got < kPageSize) break;
    if (total >= kMaxAssets) { LOG.println("[immich] hit the 10000-asset cap"); break; }
  }

  out.close();
  if (total == 0) {
    SD.remove(tmp);
    err = "album is empty (or contains no photos)";
    return 0;
  }
  // Swap in atomically so an interrupted fetch never leaves a half playlist.
  SD.remove(SD_PATH_PLAYLIST);
  if (!SD.rename(tmp, SD_PATH_PLAYLIST)) {
    SD.remove(tmp);
    err = "cannot replace playlist file";
    return -1;
  }
  LOG.printf("[immich] playlist: %d photos\n", total);
  return total;
}

bool immichDownloadAsset(const String& assetId, const char* destPath, String& err) {
  if (!sdReady()) { err = "no SD card"; return false; }

  // `original` is served by a different endpoint; the other three are
  // renditions of the thumbnail endpoint.
  String url;
  if (g_cfg.imageSize == IMG_ORIGINAL) {
    url = baseUrl() + "/api/assets/" + assetId + "/original";
  } else {
    url = baseUrl() + "/api/assets/" + assetId + "/thumbnail?size=" +
          imageSizeName(g_cfg.imageSize);
  }

  Request r;
  if (!r.begin(url)) { err = "cannot open connection"; return false; }
  r.http.addHeader("Accept", "image/jpeg,image/png,*/*");

  const int code = r.http.GET();
  if (code != 200) { err = httpErr(code, r.http); return false; }

  const int len = r.http.getSize();
  SD.remove(destPath);
  File f = SD.open(destPath, FILE_WRITE);
  if (!f) { err = "cannot write to SD"; return false; }

  WiFiClient* stream = r.http.getStreamPtr();
  uint8_t buf[2048];
  size_t written = 0;
  const uint32_t t0 = millis();
  uint32_t lastData = t0;

  // Content-Length may be absent (chunked transfer), so the loop is driven by
  // "is there more data or is the connection still open", with an idle timeout
  // rather than a total one -- a slow link should not abort a good download.
  while (true) {
    const size_t avail = stream->available();
    if (avail) {
      const int n = stream->readBytes(buf, min(avail, sizeof(buf)));
      if (n <= 0) break;
      if (f.write(buf, n) != (size_t)n) {
        f.close(); SD.remove(destPath); err = "SD write failed"; return false;
      }
      written += n;
      lastData = millis();
      if (len > 0 && written >= (size_t)len) break;
      continue;
    }
    if (!stream->connected() && !stream->available()) break;
    if (millis() - lastData > 20000) {
      f.close(); SD.remove(destPath); err = "download stalled"; return false;
    }
    delay(2);
  }
  f.close();

  if (written == 0) { SD.remove(destPath); err = "server returned an empty image"; return false; }
  if (len > 0 && written != (size_t)len) {
    SD.remove(destPath);
    err = "truncated download (" + String((unsigned)written) + "/" + String(len) + " bytes)";
    return false;
  }
  LOG.printf("[immich] downloaded %u kB in %lu ms (%s)\n", (unsigned)(written / 1024),
             (unsigned long)(millis() - t0), imageSizeName(g_cfg.imageSize));
  return true;
}
