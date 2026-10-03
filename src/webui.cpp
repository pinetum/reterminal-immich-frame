#include "webui.h"
#include "web_assets.h"
#include "app.h"
#include "settings.h"
#include "net.h"
#include "playlist.h"
#include "log.h"
#include "render.h"
#include "sdcard.h"

#include <SD.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>

static AsyncWebServer* s_srv = nullptr;

// ---------------------------------------------------------------------------
// Long-running work NEVER happens on the webserver task: a panel refresh blocks
// for ~40 s and would stall the HTTP stack. Requests record an intent in
// g_status.action and return 202 immediately; main.cpp's loop() runs it and the
// page watches /api/status for progress.
// ---------------------------------------------------------------------------

static bool authed(AsyncWebServerRequest* req) {
  if (g_cfg.adminUser.isEmpty() && g_cfg.adminPass.isEmpty()) return true;
  if (req->authenticate(g_cfg.adminUser.c_str(), g_cfg.adminPass.c_str())) return true;
  req->requestAuthentication();
  return false;
}

static void sendStatus(AsyncWebServerRequest* req) {
  // Deliberately does NOT call appRefreshStatus(): that reads the battery ADC
  // and the SD card, and this runs on the AsyncTCP task. The main task keeps
  // g_status up to date; here we only serialise it.
  JsonDocument d;
  d["photos"]         = g_status.photos;
  d["position"]       = g_status.position;
  d["batteryPct"]     = g_status.batteryPct;
  d["batteryV"]       = g_status.batteryV;
  d["sdOk"]           = g_status.sdOk;
  d["cacheFrames"]    = g_status.cacheFrames;
  d["renderMs"]       = g_status.renderMs;
  d["rotation"]       = g_status.rotation;
  d["asset"]          = g_status.currentAsset;
  d["lastError"]      = g_status.lastError;
  d["busy"]           = g_status.busy;
  d["ip"]             = netIp();
  d["rssi"]           = netRssi();
  d["ssid"]           = netSsid();
  d["apMode"]         = netIsAp();
  d["psramFreeKb"]    = (uint32_t)(ESP.getFreePsram() / 1024);
  d["playlistAgeMin"] = playlistAgeMinutes();
  d["testMsg"]        = g_status.testMsg;
  d["testOk"]         = g_status.testOk;
  d["albumsErr"]      = g_status.albumsErr;
  d["albumsReady"]    = g_status.albumsJson.length() > 2;
  d["scanReady"]      = g_status.scanJson.length() > 2;
  d["previewReady"]   = g_status.previewReady;
  d["previewErr"]     = g_status.previewErr;
  d["previewRot"]     = g_status.previewRotation;
  d["previewW"]       = PREVIEW_W;
  d["previewH"]       = PREVIEW_H;
  // The page needs to know an action is pending but not yet started, otherwise
  // it would see an empty `busy` on its first poll and declare victory early.
  d["queued"]         = g_status.action != ACT_NONE;
  const long left = (long)g_status.configEndsAt - (long)millis();
  d["windowLeft"] = left > 0 ? left / 1000 : 0;

  String out;
  serializeJson(d, out);
  req->send(200, "application/json", out);
}

static void sendConfig(AsyncWebServerRequest* req) {
  JsonDocument d;
  d["wifiSsid"]  = g_cfg.wifiSsid;
  d["immichUrl"] = g_cfg.immichUrl;
  d["albumId"]   = g_cfg.albumId;
  d["albumName"] = g_cfg.albumName;
  d["intervalMinutes"]     = g_cfg.intervalMinutes;
  d["shuffle"]             = g_cfg.shuffle;
  d["imageSize"]           = (int)g_cfg.imageSize;
  d["playlistTtlHours"]    = g_cfg.playlistTtlHours;
  d["gamma"]               = g_cfg.gamma;
  d["fit"]                 = (int)g_cfg.fit;
  d["rotation"]            = g_cfg.rotation;
  d["showFooter"]          = g_cfg.showFooter;
  d["configWindowMinutes"] = g_cfg.configWindowMinutes;
  d["lowBatteryPercent"]   = g_cfg.lowBatteryPercent;
  d["cacheFrames"]         = g_cfg.cacheFrames;
  d["adminUser"]           = g_cfg.adminUser;

  // --- calibrated palette ---
  d["paletteId"] = g_cfg.paletteId;
  {
    // Always report the CUSTOM slots, never the resolved palette: the six text
    // fields in the admin page edit the custom entry, and overwriting them with
    // whichever built-in is currently selected would quietly destroy the user's
    // measurements the first time they looked at a built-in.
    JsonArray a = d["paletteCustom"].to<JsonArray>();
    char hex[8];
    for (int i = 0; i < PAL_SLOTS; ++i) {
      paletteHexFormat(g_cfg.paletteCustom.rgb[i], hex);
      a.add(hex);
    }
    // The built-in tables, so the page can show swatches and prefill the custom
    // fields from one of them without hardcoding a second copy of the values.
    JsonObject b = d["paletteBuiltins"].to<JsonObject>();
    for (int id = 0; id < PALETTE_IDS; ++id) {
      JsonArray e = b[paletteIdName(id)].to<JsonArray>();
      const E6Palette& p = paletteBuiltin(id);
      for (int i = 0; i < PAL_SLOTS; ++i) { paletteHexFormat(p.rgb[i], hex); e.add(hex); }
    }
  }

  // --- dithering ---
  d["ditherType"]  = (int)g_cfg.dither.type;
  d["edMatrix"]    = (int)g_cfg.dither.matrix;
  d["colorMatch"]  = (int)g_cfg.dither.matching;
  d["serpentine"]  = g_cfg.dither.serpentine;
  d["bayerSize"]   = g_cfg.dither.bayerSize;
  d["orderedStrength"] = g_cfg.dither.orderedStrength;

  // --- pre-dither processing ---
  const ProcSettings& ps = g_cfg.proc;
  d["procPreset"]  = ps.preset;
  d["exposure"]    = ps.exposure;
  d["saturation"]  = ps.saturation;
  d["toneMode"]    = ps.toneMode;
  d["contrast"]    = ps.contrast;
  d["scStrength"]  = ps.scStrength;
  d["scShadow"]    = ps.scShadow;
  d["scHighlight"] = ps.scHighlight;
  d["scMidpoint"]  = ps.scMidpoint;
  d["drcMode"]     = ps.drcMode;
  d["drcStrength"] = ps.drcStrength;
  d["drcLowPct"]   = ps.drcLowPct;
  d["drcHighPct"]  = ps.drcHighPct;
  d["drcQuality"]  = ps.drcQuality;
  d["drcPreserveWhite"] = ps.drcPreserveWhite;
  d["levelMode"]   = ps.levelMode;
  d["levelAuto"]   = ps.levelAuto;
  d["clarityAmount"]  = ps.clarityAmount;
  d["clarityRadius"]  = ps.clarityRadius;
  d["clarityMidtone"] = ps.clarityMidtone;
  d["paperMode"]      = ps.paperMode;
  d["paperStrength"]  = ps.paperStrength;
  // Secrets are never sent back out, only whether one is stored. The page sends
  // a secret only when the user types a new one; blank means "keep".
  d["hasWifiPass"]  = g_cfg.wifiPass.length() > 0;
  d["hasImmichKey"] = g_cfg.immichKey.length() > 0;
  d["hasAdminPass"] = g_cfg.adminPass.length() > 0;

  String out;
  serializeJson(d, out);
  req->send(200, "application/json", out);
}

static void applyConfig(AsyncWebServerRequest* req, JsonVariant& json) {
  if (!authed(req)) return;
  JsonObject d = json.as<JsonObject>();
  if (d.isNull()) { req->send(400, "text/plain", "expected a JSON object"); return; }

  const String   oldAlbum = g_cfg.albumId;
  const uint32_t oldRenderSig = settingsRenderSignature(g_cfg);

  if (d["wifiSsid"].is<const char*>())  g_cfg.wifiSsid  = d["wifiSsid"].as<String>();
  if (d["immichUrl"].is<const char*>()) g_cfg.immichUrl = d["immichUrl"].as<String>();
  if (d["albumId"].is<const char*>())   g_cfg.albumId   = d["albumId"].as<String>();
  if (d["albumName"].is<const char*>()) g_cfg.albumName = d["albumName"].as<String>();
  if (d["adminUser"].is<const char*>()) g_cfg.adminUser = d["adminUser"].as<String>();

  // Only overwrite a secret when one was actually supplied.
  if (d["wifiPass"].is<const char*>())  g_cfg.wifiPass  = d["wifiPass"].as<String>();
  if (d["immichKey"].is<const char*>()) g_cfg.immichKey = d["immichKey"].as<String>();
  if (d["adminPass"].is<const char*>()) g_cfg.adminPass = d["adminPass"].as<String>();
  if (d["caPem"].is<const char*>())     g_cfg.caPem     = d["caPem"].as<String>();

  if (!d["intervalMinutes"].isNull())     g_cfg.intervalMinutes   = d["intervalMinutes"].as<uint32_t>();
  if (!d["shuffle"].isNull())             g_cfg.shuffle           = d["shuffle"].as<bool>();
  if (!d["imageSize"].isNull())           g_cfg.imageSize         = (ImageSize)d["imageSize"].as<int>();
  if (!d["playlistTtlHours"].isNull())    g_cfg.playlistTtlHours  = d["playlistTtlHours"].as<uint32_t>();
  if (!d["gamma"].isNull())               g_cfg.gamma             = d["gamma"].as<float>();
  if (!d["fit"].isNull())                 g_cfg.fit               = (FitMode)d["fit"].as<int>();
  if (!d["rotation"].isNull())            g_cfg.rotation          = d["rotation"].as<int>();
  if (!d["showFooter"].isNull())          g_cfg.showFooter        = d["showFooter"].as<bool>();
  if (!d["configWindowMinutes"].isNull()) g_cfg.configWindowMinutes = d["configWindowMinutes"].as<uint32_t>();
  if (!d["lowBatteryPercent"].isNull())   g_cfg.lowBatteryPercent = d["lowBatteryPercent"].as<uint32_t>();
  if (!d["cacheFrames"].isNull())         g_cfg.cacheFrames       = d["cacheFrames"].as<uint32_t>();

  // --- calibrated palette ---
  if (!d["paletteId"].isNull()) g_cfg.paletteId = (uint8_t)d["paletteId"].as<int>();
  if (d["paletteCustom"].is<JsonArray>()) {
    JsonArray a = d["paletteCustom"].as<JsonArray>();
    for (int i = 0; i < PAL_SLOTS && i < (int)a.size(); ++i) {
      const char* hex = a[i].as<const char*>();
      // A slot that will not parse keeps its stored value rather than becoming
      // black, which is what a naive parse-or-zero would do to a typo.
      if (hex) paletteHexParse(hex, g_cfg.paletteCustom.rgb[i]);
    }
  }

  // --- dithering ---
  if (!d["ditherType"].isNull()) g_cfg.dither.type     = (DitherType)d["ditherType"].as<int>();
  if (!d["edMatrix"].isNull())   g_cfg.dither.matrix   = (EdMatrix)d["edMatrix"].as<int>();
  if (!d["colorMatch"].isNull()) g_cfg.dither.matching = (ColorMatching)d["colorMatch"].as<int>();
  if (!d["serpentine"].isNull()) g_cfg.dither.serpentine = d["serpentine"].as<bool>();
  if (!d["bayerSize"].isNull())  g_cfg.dither.bayerSize = (uint8_t)d["bayerSize"].as<int>();
  if (!d["orderedStrength"].isNull())
    g_cfg.dither.orderedStrength = (uint8_t)d["orderedStrength"].as<int>();

  // --- pre-dither processing ---
  ProcSettings& ps = g_cfg.proc;
  if (!d["procPreset"].isNull())  ps.preset      = (uint8_t)d["procPreset"].as<int>();
  if (!d["exposure"].isNull())    ps.exposure    = d["exposure"].as<float>();
  if (!d["saturation"].isNull())  ps.saturation  = d["saturation"].as<float>();
  if (!d["toneMode"].isNull())    ps.toneMode    = (uint8_t)d["toneMode"].as<int>();
  if (!d["contrast"].isNull())    ps.contrast    = d["contrast"].as<float>();
  if (!d["scStrength"].isNull())  ps.scStrength  = d["scStrength"].as<float>();
  if (!d["scShadow"].isNull())    ps.scShadow    = d["scShadow"].as<float>();
  if (!d["scHighlight"].isNull()) ps.scHighlight = d["scHighlight"].as<float>();
  if (!d["scMidpoint"].isNull())  ps.scMidpoint  = d["scMidpoint"].as<float>();
  if (!d["drcMode"].isNull())     ps.drcMode     = (uint8_t)d["drcMode"].as<int>();
  if (!d["drcStrength"].isNull()) ps.drcStrength = d["drcStrength"].as<float>();
  if (!d["drcLowPct"].isNull())   ps.drcLowPct   = d["drcLowPct"].as<float>();
  if (!d["drcHighPct"].isNull())  ps.drcHighPct  = d["drcHighPct"].as<float>();
  if (!d["drcQuality"].isNull())  ps.drcQuality  = (uint8_t)d["drcQuality"].as<int>();
  if (!d["drcPreserveWhite"].isNull()) ps.drcPreserveWhite = d["drcPreserveWhite"].as<bool>();
  if (!d["levelMode"].isNull())   ps.levelMode   = (uint8_t)d["levelMode"].as<int>();
  if (!d["levelAuto"].isNull())   ps.levelAuto   = d["levelAuto"].as<bool>();
  if (!d["clarityAmount"].isNull())  ps.clarityAmount  = d["clarityAmount"].as<float>();
  if (!d["clarityRadius"].isNull())  ps.clarityRadius  = (uint8_t)d["clarityRadius"].as<int>();
  if (!d["clarityMidtone"].isNull()) ps.clarityMidtone = d["clarityMidtone"].as<float>();
  if (!d["paperMode"].isNull())      ps.paperMode      = (uint8_t)d["paperMode"].as<int>();
  if (!d["paperStrength"].isNull())  ps.paperStrength  = d["paperStrength"].as<float>();

  // The admin page sends this when the user has just picked a preset from the
  // dropdown. The preset table lives in src/imgproc.cpp and nowhere else, so
  // the page posts the choice and reloads rather than carrying its own copy of
  // the values -- two copies of a tuning table is two copies that drift.
  if (d["applyPreset"].as<bool>()) {
    uint8_t cm = (uint8_t)g_cfg.dither.matching, ed = (uint8_t)g_cfg.dither.matrix;
    imgprocPreset(ps.preset, &ps, &cm, &ed);
    g_cfg.dither.matching = (ColorMatching)cm;
    g_cfg.dither.matrix   = (EdMatrix)ed;
    LOG.printf("[web] applied processing preset '%s'\n", procPresetName(ps.preset));
  }

  settingsSave();
  settingsLoad();   // re-run the clamping that settingsLoad() applies

  // Anything that changes what a rendered frame looks like invalidates the
  // cache, otherwise old frames would keep being shown with the old settings.
  // Flagged rather than done here: cacheClear() and playlistInit() touch the SD
  // card, and all SD access belongs to the main task.
  if (g_cfg.albumId != oldAlbum) {
    LOG.println("[web] album changed -- playlist and frame cache will be dropped");
    g_status.cacheInvalid = true;
    g_status.playlistInvalid = true;
    g_status.albumsJson = "";       // force a reload so the name matches
  } else if (settingsRenderSignature(g_cfg) != oldRenderSig) {
    // Any palette, dither or processing change makes every cached frame a
    // picture of the OLD settings. This used to check only the image size,
    // which was enough when the renderer had two knobs; with the calibrated
    // palette in play a stale cache would mask the change entirely and look
    // like the new setting simply did nothing.
    LOG.println("[web] render settings changed -- frame cache will be dropped");
    g_status.cacheInvalid = true;
  }

  req->send(200, "application/json", "{\"ok\":true}");
}

static void queueAction(AsyncWebServerRequest* req, JsonVariant& json) {
  if (!authed(req)) return;
  JsonObject d = json.as<JsonObject>();
  const String a = d["action"] | "";

  if (g_status.busy.length()) { req->send(409, "text/plain", "busy: " + g_status.busy); return; }

  // Anything that has to reach Immich is pointless from the setup access point,
  // and saying so now beats a 20-second timeout later.
  if ((a == "albums" || a == "test" || a == "playlist" || a == "next" ||
       a == "prev" || a == "refresh" || a == "preview") && netIsAp()) {
    req->send(503, "text/plain",
              "The frame is on its own setup network and cannot reach Immich yet. "
              "Fill in the Wi-Fi details, then press \"Save & reconnect\".");
    return;
  }

  if      (a == "next")     g_status.action = ACT_NEXT;
  else if (a == "prev")     g_status.action = ACT_PREV;
  else if (a == "refresh")  g_status.action = ACT_REFRESH;
  else if (a == "playlist") g_status.action = ACT_PLAYLIST_REFRESH;
  else if (a == "cache")    g_status.action = ACT_CLEAR_CACHE;
  else if (a == "albums")   g_status.action = ACT_LOAD_ALBUMS;
  else if (a == "test")     g_status.action = ACT_IMMICH_TEST;
  else if (a == "scan")     g_status.action = ACT_WIFI_SCAN;
  else if (a == "exit")     g_status.action = ACT_EXIT_CONFIG;
  else if (a == "reboot")   g_status.action = ACT_REBOOT;
  else if (a == "preview")  g_status.action = ACT_PREVIEW;
  else if (a == "chart")    g_status.action = ACT_PALETTE_CHART;
  else if (a == "index") {
    g_status.action = ACT_INDEX;
    g_status.actionArg = d["index"] | 1;
  } else {
    req->send(400, "text/plain", "unknown action");
    return;
  }
  // 202 Accepted: queued, not done. The page polls /api/status.
  req->send(202, "application/json", "{\"queued\":true}");
}

void webuiBegin() {
  if (s_srv) return;
  s_srv = new AsyncWebServer(80);

  s_srv->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!authed(req)) return;
    // Serve straight out of flash -- no 11 kB copy into the heap.
    AsyncWebServerResponse* r = req->beginResponse(
        200, "text/html", (const uint8_t*)ADMIN_PAGE, strlen(ADMIN_PAGE));
    r->addHeader("Cache-Control", "no-store");
    req->send(r);
  });

  s_srv->on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!authed(req)) return;
    sendStatus(req);
  });

  s_srv->on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!authed(req)) return;
    sendConfig(req);
  });

  // These three only READ results that the main task produced via the action
  // queue. The page posts the matching action first, waits for /api/status to
  // report it finished, then reads here.
  s_srv->on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!authed(req)) return;
    req->send(200, "application/json",
              g_status.scanJson.length() ? g_status.scanJson : String("[]"));
  });

  s_srv->on("/api/albums", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!authed(req)) return;
    if (g_status.albumsErr.length()) { req->send(502, "text/plain", g_status.albumsErr); return; }
    req->send(200, "application/json",
              g_status.albumsJson.length() ? g_status.albumsJson : String("[]"));
  });

  // The preview is raw RGB888 straight off the card -- PREVIEW_W * PREVIEW_H * 3
  // bytes, no header. The page paints it into a <canvas> with ImageData. Raw
  // because there is no image encoder in the firmware (PNGdec decodes only),
  // and beginResponse(File) streams it without a copy in RAM.
  s_srv->on("/api/preview.bin", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!authed(req)) return;
    if (!g_status.previewReady || !SD.exists(SD_PATH_PREVIEW)) {
      req->send(404, "text/plain",
                g_status.previewErr.length() ? g_status.previewErr
                                             : String("no preview has been rendered yet"));
      return;
    }
    AsyncWebServerResponse* r =
        req->beginResponse(SD, SD_PATH_PREVIEW, "application/octet-stream");
    r->addHeader("Cache-Control", "no-store");
    req->send(r);
  });

  // AsyncCallbackJsonWebHandler does the body accumulation and JSON parsing, and
  // crucially calls us exactly once with the complete document -- doing that by
  // hand with a raw body handler is the classic way to end up sending two
  // responses to one request.
  auto* cfgPost = new AsyncCallbackJsonWebHandler("/api/config", applyConfig);
  cfgPost->setMethod(HTTP_POST);
  s_srv->addHandler(cfgPost);

  auto* actPost = new AsyncCallbackJsonWebHandler("/api/action", queueAction);
  actPost->setMethod(HTTP_POST);
  s_srv->addHandler(actPost);

  // Captive portal: anything else goes to the admin page, so joining the setup
  // access point pops it up without the user typing an IP address.
  s_srv->onNotFound([](AsyncWebServerRequest* req) {
    if (netIsAp()) req->redirect("http://" + netIp() + "/");
    else           req->send(404, "text/plain", "not found");
  });

  s_srv->begin();
  LOG.printf("[web] admin page at http://%s/\n", netIp().c_str());
}

void webuiEnd() {
  if (!s_srv) return;
  s_srv->end();
  delete s_srv;
  s_srv = nullptr;
  LOG.println("[web] stopped");
}

void webuiLoop() { netLoop(); }
