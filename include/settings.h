#pragma once
#include <Arduino.h>
#include "e6_dither.h"   // DitherMethod lives with the ditherer itself

// Which Immich rendition to fetch. `preview` (1440px long edge by default) is
// the sweet spot for a 1200x1600 panel: big enough, already web-friendly JPEG,
// and an order of magnitude smaller than the original.
enum ImageSize { IMG_THUMBNAIL = 0, IMG_PREVIEW, IMG_FULLSIZE, IMG_ORIGINAL };

// How the photo is fitted onto the panel.
enum FitMode { FIT_COVER = 0, FIT_CONTAIN };

// Rotation is in degrees: 0 and 180 keep the panel's native portrait 1200x1600,
// 90 and 270 compose a 1600x1200 landscape canvas and map it onto the panel.
// 270 corresponds to physically turning the frame clockwise, 90 anticlockwise.
// ROTATION_AUTO picks 270 for landscape photos and 0 for portrait ones.
#define ROTATION_AUTO (-1)

struct Settings {
  // --- network ---
  String wifiSsid;
  String wifiPass;

  // --- Immich ---
  String immichUrl;      // e.g. "http://192.168.1.50:2283" (no trailing slash, no /api)
  String immichKey;      // sent as the x-api-key header
  String albumId;        // album UUID
  String albumName;      // cached for display only
  String caPem;          // optional CA cert for https; empty = setInsecure()

  // --- slideshow ---
  uint32_t intervalMinutes = 60;
  bool     shuffle         = false;
  ImageSize imageSize      = IMG_PREVIEW;
  uint32_t playlistTtlHours = 24;

  // --- rendering ---
  DitherMethod dither = DITHER_FS;
  float    gamma      = 1.0f;
  FitMode  fit        = FIT_COVER;
  int      rotation   = ROTATION_AUTO;
  bool     showFooter = false;

  // --- power / housekeeping ---
  uint32_t configWindowMinutes = 10;
  uint32_t lowBatteryPercent   = 5;
  uint32_t cacheFrames         = 20;

  // --- admin page ---
  String adminUser;      // empty = no HTTP basic auth
  String adminPass;

  bool configured() const { return wifiSsid.length() > 0; }
  bool immichReady() const {
    return immichUrl.length() > 0 && immichKey.length() > 0 && albumId.length() > 0;
  }
};

extern Settings g_cfg;

void settingsLoad();
void settingsSave();
void settingsFactoryReset();

const char* imageSizeName(ImageSize s);
const char* ditherName(DitherMethod d);
