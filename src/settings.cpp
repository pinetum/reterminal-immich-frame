#include "settings.h"
#include "log.h"
#include <Preferences.h>

// Settings live in NVS rather than on the SD card so that pulling the card out
// never loses the configuration. Preferences keys are capped at 15 chars.
static const char* NS = "frame";

Settings g_cfg;

namespace {

// The pre-calibration dither setting, kept only so settingsLoad() can migrate
// a device that was configured before DitherCfg existed.
enum LegacyDither { LEG_NONE = 0, LEG_BAYER8, LEG_FS, LEG_JARVIS, LEG_ATKINSON };

// Absent-key sentinels. Preferences::isKey() is not available on every
// arduino-esp32 release this project builds against, so presence is detected
// by handing get*() a value the field can never legitimately hold.
constexpr uint8_t kNoU8 = 0xFF;

void migrateDither(Preferences& p, DitherCfg* d) {
  const uint8_t legacy = p.getUChar("dither", kNoU8);
  if (legacy == kNoU8) return;   // nothing to migrate either

  switch (legacy) {
    case LEG_NONE:     d->type = DITHER_QUANTIZE_ONLY; break;
    case LEG_BAYER8:   d->type = DITHER_ORDERED; d->bayerSize = 8; break;
    case LEG_JARVIS:   d->type = DITHER_ERROR_DIFFUSION; d->matrix = ED_JARVIS; break;
    case LEG_ATKINSON: d->type = DITHER_ERROR_DIFFUSION; d->matrix = ED_ATKINSON; break;
    case LEG_FS:
    default:           d->type = DITHER_ERROR_DIFFUSION; d->matrix = ED_FLOYD_STEINBERG; break;
  }
  LOG.printf("[cfg] migrated legacy dither=%u -> %s/%s\n", (unsigned)legacy,
             ditherTypeName(d->type), edMatrixName(d->matrix));
}

inline void fnv(uint32_t* h, const void* data, size_t n) {
  const uint8_t* p = (const uint8_t*)data;
  for (size_t i = 0; i < n; ++i) {
    *h ^= p[i];
    *h *= 16777619u;
  }
}

template <typename T>
inline void fnvVal(uint32_t* h, const T& v) { fnv(h, &v, sizeof(T)); }

}  // namespace

const E6Palette& settingsPalette(const Settings& cfg) {
  return cfg.paletteId == PAL_CUSTOM ? cfg.paletteCustom : paletteBuiltin(cfg.paletteId);
}

uint32_t settingsRenderSignature(const Settings& cfg) {
  uint32_t h = 2166136261u;

  fnvVal(&h, cfg.gamma);
  fnvVal(&h, (uint8_t)cfg.fit);
  fnvVal(&h, (int32_t)cfg.rotation);
  fnvVal(&h, (uint8_t)cfg.showFooter);
  fnvVal(&h, (uint8_t)cfg.imageSize);

  // The resolved palette rather than the id, so switching between two ids that
  // happen to hold the same values does not needlessly drop the cache.
  fnv(&h, settingsPalette(cfg).rgb, sizeof(E6Palette::rgb));

  fnvVal(&h, (uint8_t)cfg.dither.type);
  fnvVal(&h, (uint8_t)cfg.dither.matrix);
  fnvVal(&h, (uint8_t)cfg.dither.matching);
  fnvVal(&h, (uint8_t)cfg.dither.serpentine);
  fnvVal(&h, cfg.dither.bayerSize);
  fnvVal(&h, cfg.dither.orderedStrength);

  // ProcSettings is a plain aggregate of scalars with no padding worth caring
  // about, but hash the fields explicitly anyway: hashing the struct would make
  // the signature depend on compiler padding, and a signature that changes
  // across a rebuild would wipe every user's cache for nothing.
  const ProcSettings& ps = cfg.proc;
  fnvVal(&h, ps.exposure);    fnvVal(&h, ps.saturation);
  fnvVal(&h, ps.toneMode);    fnvVal(&h, ps.contrast);
  fnvVal(&h, ps.scStrength);  fnvVal(&h, ps.scShadow);
  fnvVal(&h, ps.scHighlight); fnvVal(&h, ps.scMidpoint);
  fnvVal(&h, ps.drcMode);     fnvVal(&h, ps.drcStrength);
  fnvVal(&h, ps.drcLowPct);   fnvVal(&h, ps.drcHighPct);
  fnvVal(&h, ps.drcQuality);  fnvVal(&h, (uint8_t)ps.drcPreserveWhite);
  fnvVal(&h, ps.levelMode);   fnvVal(&h, (uint8_t)ps.levelAuto);
  fnvVal(&h, ps.clarityAmount); fnvVal(&h, ps.clarityRadius);
  fnvVal(&h, ps.clarityMidtone);
  fnvVal(&h, ps.paperMode);   fnvVal(&h, ps.paperStrength);

  return h;
}

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

  g_cfg.gamma      = p.getFloat("gamma", 1.0f);
  g_cfg.fit        = (FitMode)p.getUChar("fit", FIT_COVER);
  g_cfg.rotation   = p.getInt("rotation", ROTATION_AUTO);
  g_cfg.showFooter = p.getBool("footer", false);

  // --- palette ---
  g_cfg.paletteId = p.getUChar("palId", PAL_SEEED);
  g_cfg.paletteCustom = paletteBuiltin(PAL_SPECTRA6);
  p.getBytes("palCustom", g_cfg.paletteCustom.rgb, sizeof(E6Palette::rgb));

  // --- dithering ---
  const uint8_t dithType = p.getUChar("dithType", kNoU8);
  if (dithType == kNoU8) {
    // Written before this firmware grew a dither type. Carry the old single
    // setting over so the device keeps rendering what it rendered yesterday.
    migrateDither(p, &g_cfg.dither);
  } else {
    g_cfg.dither.type       = (DitherType)dithType;
    g_cfg.dither.matrix     = (EdMatrix)p.getUChar("edMatrix", ED_FLOYD_STEINBERG);
    g_cfg.dither.matching   = (ColorMatching)p.getUChar("colorMatch", CM_RGB);
    g_cfg.dither.serpentine = p.getBool("serp", false);
    g_cfg.dither.bayerSize  = p.getUChar("bayerSz", 8);
    g_cfg.dither.orderedStrength = p.getUChar("ordStr", 64);
  }

  // --- pre-dither processing ---
  ProcSettings& ps = g_cfg.proc;
  ps.preset      = p.getUChar("procPreset", PROC_CUSTOM);
  ps.exposure    = p.getFloat("expo", 1.0f);
  ps.saturation  = p.getFloat("sat", 1.0f);
  ps.toneMode    = p.getUChar("toneMode", TONE_OFF);
  ps.contrast    = p.getFloat("contrast", 1.0f);
  ps.scStrength  = p.getFloat("scStr", 0.9f);
  ps.scShadow    = p.getFloat("scShad", 0.0f);
  ps.scHighlight = p.getFloat("scHigh", 1.5f);
  ps.scMidpoint  = p.getFloat("scMid", 0.5f);
  ps.drcMode     = p.getUChar("drcMode", DRC_OFF);
  ps.drcStrength = p.getFloat("drcStr", 1.0f);
  ps.drcLowPct   = p.getFloat("drcLoP", 0.01f);
  ps.drcHighPct  = p.getFloat("drcHiP", 0.99f);
  ps.drcQuality  = p.getUChar("drcQual", DRCQ_FAST);
  ps.drcPreserveWhite = p.getBool("drcPresW", false);
  ps.levelMode   = p.getUChar("lvlMode", LVL_OFF);
  ps.levelAuto   = p.getBool("lvlAuto", false);
  ps.clarityAmount  = p.getFloat("clarAmt", 0.0f);
  ps.clarityRadius  = p.getUChar("clarRad", 2);
  ps.clarityMidtone = p.getFloat("clarMid", 1.2f);
  ps.paperMode      = p.getUChar("paperMode", PAPER_OFF);
  ps.paperStrength  = p.getFloat("paperStr", 1.0f);

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

  // Enum fields are clamped rather than trusted: a corrupt NVS byte would
  // otherwise index a kernel table out of bounds.
  if (g_cfg.paletteId >= PALETTE_IDS)        g_cfg.paletteId = PAL_SEEED;
  if (g_cfg.dither.type >= DITHER_TYPES)     g_cfg.dither.type = DITHER_ERROR_DIFFUSION;
  if (g_cfg.dither.matrix >= ED_MATRICES)    g_cfg.dither.matrix = ED_FLOYD_STEINBERG;
  if (g_cfg.dither.matching > CM_CHROMA)     g_cfg.dither.matching = CM_RGB;
  if (g_cfg.dither.bayerSize != 2 && g_cfg.dither.bayerSize != 4 &&
      g_cfg.dither.bayerSize != 8 && g_cfg.dither.bayerSize != 16)
    g_cfg.dither.bayerSize = 8;
  if (g_cfg.dither.orderedStrength < 1) g_cfg.dither.orderedStrength = 1;

  if (ps.preset >= PROC_PRESETS)   ps.preset = PROC_CUSTOM;
  if (ps.toneMode >= TONE_MODES)   ps.toneMode = TONE_OFF;
  if (ps.drcMode >= DRC_MODES)     ps.drcMode = DRC_OFF;
  if (ps.drcQuality > DRCQ_ACCURATE) ps.drcQuality = DRCQ_FAST;
  if (ps.levelMode >= LVL_MODES)   ps.levelMode = LVL_OFF;
  if (ps.paperMode >= PAPER_MODES) ps.paperMode = PAPER_OFF;

  // Numeric ranges match the sliders the admin page offers, which in turn come
  // from ProcessingControls.vue in esp32-photoframe.
  ps.exposure    = constrain(ps.exposure, 0.5f, 2.0f);
  ps.saturation  = constrain(ps.saturation, 0.0f, 2.0f);
  ps.contrast    = constrain(ps.contrast, 0.5f, 2.0f);
  ps.scStrength  = constrain(ps.scStrength, 0.0f, 1.0f);
  ps.scShadow    = constrain(ps.scShadow, 0.0f, 1.0f);
  ps.scHighlight = constrain(ps.scHighlight, 0.5f, 5.0f);
  ps.scMidpoint  = constrain(ps.scMidpoint, 0.3f, 0.7f);
  ps.drcStrength = constrain(ps.drcStrength, 0.0f, 1.0f);
  ps.drcLowPct   = constrain(ps.drcLowPct, 0.0f, 0.4f);
  ps.drcHighPct  = constrain(ps.drcHighPct, 0.6f, 1.0f);
  ps.clarityAmount  = constrain(ps.clarityAmount, -1.0f, 1.0f);
  ps.clarityMidtone = constrain(ps.clarityMidtone, 0.1f, 4.0f);
  ps.paperStrength  = constrain(ps.paperStrength, 0.0f, 1.0f);
  if (ps.clarityRadius < 1) ps.clarityRadius = 1;
  if (ps.clarityRadius > 4) ps.clarityRadius = 4;

  LOG.printf("[cfg] loaded: ssid='%s' immich='%s' album='%s' interval=%umin size=%s\n",
             g_cfg.wifiSsid.c_str(), g_cfg.immichUrl.c_str(), g_cfg.albumId.c_str(),
             (unsigned)g_cfg.intervalMinutes, imageSizeName(g_cfg.imageSize));
  LOG.printf("[cfg] render: palette=%s dither=%s/%s match=%s%s preset=%s tone=%s drc=%s%s\n",
             paletteIdName(g_cfg.paletteId), ditherTypeName(g_cfg.dither.type),
             edMatrixName(g_cfg.dither.matrix), colorMatchName(g_cfg.dither.matching),
             g_cfg.dither.serpentine ? " serpentine" : "", procPresetName(ps.preset),
             toneModeName(ps.toneMode), drcModeName(ps.drcMode),
             ps.drcQuality == DRCQ_ACCURATE ? "/accurate" : "");
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

  p.putFloat("gamma", g_cfg.gamma);
  p.putUChar("fit", (uint8_t)g_cfg.fit);
  p.putInt("rotation", g_cfg.rotation);
  p.putBool("footer", g_cfg.showFooter);

  p.putUChar("palId", g_cfg.paletteId);
  p.putBytes("palCustom", g_cfg.paletteCustom.rgb, sizeof(E6Palette::rgb));

  p.putUChar("dithType", (uint8_t)g_cfg.dither.type);
  p.putUChar("edMatrix", (uint8_t)g_cfg.dither.matrix);
  p.putUChar("colorMatch", (uint8_t)g_cfg.dither.matching);
  p.putBool("serp", g_cfg.dither.serpentine);
  p.putUChar("bayerSz", g_cfg.dither.bayerSize);
  p.putUChar("ordStr", g_cfg.dither.orderedStrength);

  const ProcSettings& ps = g_cfg.proc;
  p.putUChar("procPreset", ps.preset);
  p.putFloat("expo", ps.exposure);
  p.putFloat("sat", ps.saturation);
  p.putUChar("toneMode", ps.toneMode);
  p.putFloat("contrast", ps.contrast);
  p.putFloat("scStr", ps.scStrength);
  p.putFloat("scShad", ps.scShadow);
  p.putFloat("scHigh", ps.scHighlight);
  p.putFloat("scMid", ps.scMidpoint);
  p.putUChar("drcMode", ps.drcMode);
  p.putFloat("drcStr", ps.drcStrength);
  p.putFloat("drcLoP", ps.drcLowPct);
  p.putFloat("drcHiP", ps.drcHighPct);
  p.putUChar("drcQual", ps.drcQuality);
  p.putBool("drcPresW", ps.drcPreserveWhite);
  p.putUChar("lvlMode", ps.levelMode);
  p.putBool("lvlAuto", ps.levelAuto);
  p.putFloat("clarAmt", ps.clarityAmount);
  p.putUChar("clarRad", ps.clarityRadius);
  p.putFloat("clarMid", ps.clarityMidtone);
  p.putUChar("paperMode", ps.paperMode);
  p.putFloat("paperStr", ps.paperStrength);

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
