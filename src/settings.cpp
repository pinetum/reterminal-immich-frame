#include "settings.h"
#include "log.h"
#include <Preferences.h>

// Settings live in NVS rather than on the SD card so that pulling the card out
// never loses the configuration. Preferences keys are capped at 15 chars.
static const char* NS = "frame";

Settings g_cfg;

void settingsLoad() {
  Preferences p;
  if (!p.begin(NS, /*readOnly=*/true)) {
    LOG.println("[cfg] no namespace yet -- using defaults (first boot)");
    return;
  }
  g_cfg.wifiSsid  = p.getString("wifiSsid", "");
  g_cfg.wifiPass  = p.getString("wifiPass", "");
  g_cfg.immichUrl = p.getString("immichUrl", "");
  g_cfg.immichKey = p.getString("immichKey", "");
  g_cfg.albumId   = p.getString("albumId", "");
  g_cfg.albumName = p.getString("albumName", "");
  g_cfg.caPem     = p.getString("caPem", "");

  g_cfg.intervalMinutes  = p.getUInt("interval", 60);
  g_cfg.shuffle          = p.getBool("shuffle", false);
  g_cfg.imageSize        = (ImageSize)p.getUChar("imgSize", IMG_PREVIEW);
  g_cfg.playlistTtlHours = p.getUInt("plTtl", 24);

  g_cfg.dither     = (DitherMethod)p.getUChar("dither", DITHER_FS);
  g_cfg.gamma      = p.getFloat("gamma", 1.0f);
  g_cfg.fit        = (FitMode)p.getUChar("fit", FIT_COVER);
  g_cfg.rotation   = p.getInt("rotation", ROTATION_AUTO);
  g_cfg.showFooter = p.getBool("footer", false);

  g_cfg.configWindowMinutes = p.getUInt("cfgWindow", 10);
  g_cfg.lowBatteryPercent   = p.getUInt("lowBatt", 5);
  g_cfg.cacheFrames         = p.getUInt("cacheN", 20);

  g_cfg.adminUser = p.getString("adminUser", "");
  g_cfg.adminPass = p.getString("adminPass", "");
  p.end();

  // Guard rails: a bad value here would otherwise brick the slideshow (e.g. a
  // 0-minute interval turns into a busy reboot loop that flattens the battery).
  if (g_cfg.intervalMinutes < 1)     g_cfg.intervalMinutes = 1;
  if (g_cfg.intervalMinutes > 10080) g_cfg.intervalMinutes = 10080;   // 1 week
  if (g_cfg.playlistTtlHours < 1)    g_cfg.playlistTtlHours = 1;
  if (g_cfg.configWindowMinutes < 1) g_cfg.configWindowMinutes = 1;
  if (g_cfg.configWindowMinutes > 60) g_cfg.configWindowMinutes = 60;
  if (g_cfg.gamma < 0.2f || g_cfg.gamma > 4.0f) g_cfg.gamma = 1.0f;
  if (g_cfg.cacheFrames > 200) g_cfg.cacheFrames = 200;
  if (g_cfg.lowBatteryPercent > 50) g_cfg.lowBatteryPercent = 50;

  LOG.printf("[cfg] loaded: ssid='%s' immich='%s' album='%s' interval=%umin size=%s\n",
             g_cfg.wifiSsid.c_str(), g_cfg.immichUrl.c_str(), g_cfg.albumId.c_str(),
             (unsigned)g_cfg.intervalMinutes, imageSizeName(g_cfg.imageSize));
}

void settingsSave() {
  Preferences p;
  if (!p.begin(NS, /*readOnly=*/false)) {
    LOG.println("[cfg] ERROR: cannot open NVS for write");
    return;
  }
  p.putString("wifiSsid", g_cfg.wifiSsid);
  p.putString("wifiPass", g_cfg.wifiPass);
  p.putString("immichUrl", g_cfg.immichUrl);
  p.putString("immichKey", g_cfg.immichKey);
  p.putString("albumId", g_cfg.albumId);
  p.putString("albumName", g_cfg.albumName);
  p.putString("caPem", g_cfg.caPem);

  p.putUInt("interval", g_cfg.intervalMinutes);
  p.putBool("shuffle", g_cfg.shuffle);
  p.putUChar("imgSize", (uint8_t)g_cfg.imageSize);
  p.putUInt("plTtl", g_cfg.playlistTtlHours);

  p.putUChar("dither", (uint8_t)g_cfg.dither);
  p.putFloat("gamma", g_cfg.gamma);
  p.putUChar("fit", (uint8_t)g_cfg.fit);
  p.putInt("rotation", g_cfg.rotation);
  p.putBool("footer", g_cfg.showFooter);

  p.putUInt("cfgWindow", g_cfg.configWindowMinutes);
  p.putUInt("lowBatt", g_cfg.lowBatteryPercent);
  p.putUInt("cacheN", g_cfg.cacheFrames);

  p.putString("adminUser", g_cfg.adminUser);
  p.putString("adminPass", g_cfg.adminPass);
  p.end();
  LOG.println("[cfg] saved to NVS");
}

void settingsFactoryReset() {
  Preferences p;
  if (p.begin(NS, false)) { p.clear(); p.end(); }
  g_cfg = Settings();
  LOG.println("[cfg] factory reset");
}

const char* imageSizeName(ImageSize s) {
  switch (s) {
    case IMG_THUMBNAIL: return "thumbnail";
    case IMG_PREVIEW:   return "preview";
    case IMG_FULLSIZE:  return "fullsize";
    case IMG_ORIGINAL:  return "original";
  }
  return "preview";
}

const char* ditherName(DitherMethod d) {
  switch (d) {
    case DITHER_NONE:     return "none";
    case DITHER_BAYER8:   return "bayer8";
    case DITHER_FS:       return "floyd-steinberg";
    case DITHER_JARVIS:   return "jarvis";
    case DITHER_ATKINSON: return "atkinson";
  }
  return "floyd-steinberg";
}
