#pragma once
#include <Arduino.h>
#include "palette.h"

// =============================================================================
//  Pre-dither image processing.
//
//  WHY THIS EXISTS
//  ---------------
//  A photo arrives prepared for an emissive screen: full sRGB gamut, black
//  really black, white really white. The panel has six muted inks and a white
//  point around 72% reflectance. Handing the photo straight to a ditherer
//  wastes most of the decisions on tones the panel cannot separate, which is
//  why both reference projects put an adjustment stack in front of it.
//
//  STAGE ORDER
//  -----------
//  Follows applyImageProcessing() in epdoptimize/src/dither/processing.ts and
//  preprocessImage() in epaper-image-convert/src/processor.js:
//
//      gamma -> paper normalisation -> clarity          (source pass, below)
//      exposure -> saturation -> tone -> dynamic range
//                            -> level -> white preserve (row pass, below)
//
//  (`gamma` is this firmware's own pre-existing knob, not something either
//  reference has. It overlaps `exposure`; it is kept because devices in the
//  field are configured with it, and it runs first so the rest of the chain
//  sees what it always did.)
//
//  WHY IT IS SPLIT IN THREE
//  ------------------------
//  The references process a canvas that is already at output size, so they can
//  walk the whole image as many times as they like. We cannot: a 1200x1600
//  RGB888 intermediate is 5.8 MB and we have a 960 kB frame plus a multi-MB
//  decoded source already resident. esp32-photoframe hit the same wall ("the
//  previous pipeline allocated two full panel-size RGB888 intermediates, ~15 MB
//  peak, which cannot fit in 8 MB of PSRAM") and solved it the same way: keep
//  everything row-local.
//
//  So the stages are sorted by what they actually need:
//
//    imgprocSourcePass()  the two stages that need a 2D neighbourhood or must
//                         run before scaling. Streams over the decoded source
//                         in place with a ~78 kB sliding window.
//    imgprocAnalyze()     one read-only subsampled pass to resolve the
//                         percentile-driven "auto" modes into numbers, plus
//                         every lookup table.
//    imgprocRow()         everything pointwise, applied to one already-scaled
//                         output row of RGB888 just before it is dithered. No
//                         intermediate, and no re-quantisation to RGB565.
// =============================================================================

enum ToneMode    { TONE_OFF = 0, TONE_CONTRAST, TONE_SCURVE, TONE_MODES };
enum DrcMode     { DRC_OFF = 0, DRC_DISPLAY, DRC_AUTO, DRC_MODES };
enum DrcQuality  { DRCQ_FAST = 0, DRCQ_ACCURATE };
enum LevelMode   { LVL_OFF = 0, LVL_PER_CHANNEL, LVL_LUMA, LVL_MODES };
enum PaperMode   { PAPER_OFF = 0, PAPER_WARM, PAPER_MODES };

// epdoptimize's seven presets, plus "custom" meaning "every stage neutral",
// which is this firmware's default so that an existing device is unaffected by
// the update.
enum ProcPreset {
  PROC_CUSTOM = 0,
  PROC_BALANCED,
  PROC_DYNAMIC,
  PROC_VIVID,
  PROC_SOFT,
  PROC_GRAYSCALE,
  PROC_RESTORE,
  PROC_POSTERSCAN,
  PROC_PRESETS
};

// All adjustment knobs.
//
// Values use the MULTIPLIER convention of epaper-image-convert and
// esp32-photoframe -- exposure/saturation/contrast are 1.0 at neutral and
// highlightCompress is positive -- not epdoptimize's additive convention
// (0.0 neutral, exposure in stops, highlightCompress negative). The two are
// interchangeable; multipliers give the better slider ranges and match the C
// reference, so the preset table converts epdoptimize's values once.
struct ProcSettings {
  uint8_t preset = PROC_CUSTOM;

  // --- tone ---
  float   exposure   = 1.0f;    // 0.5 .. 2.0
  float   saturation = 1.0f;    // 0.5 .. 2.0 (0.0 = monochrome)
  uint8_t toneMode   = TONE_OFF;
  float   contrast   = 1.0f;    // 0.5 .. 2.0, TONE_CONTRAST only
  float   scStrength = 0.9f;    // 0 .. 1,    TONE_SCURVE only
  float   scShadow   = 0.0f;    // 0 .. 1
  float   scHighlight= 1.5f;    // 0.5 .. 5
  float   scMidpoint = 0.5f;    // 0.3 .. 0.7

  // --- dynamic range compression ---
  uint8_t drcMode     = DRC_OFF;
  float   drcStrength = 1.0f;   // 0 .. 1
  float   drcLowPct   = 0.01f;  // DRC_AUTO only
  float   drcHighPct  = 0.99f;
  uint8_t drcQuality  = DRCQ_FAST;
  bool    drcPreserveWhite = false;

  // --- level compression (the legacy range remap; DRC_DISPLAY overlaps it) ---
  uint8_t levelMode = LVL_OFF;
  bool    levelAuto = false;

  // --- clarity (local midtone contrast) ---
  float   clarityAmount  = 0.0f;  // -1 .. 1
  uint8_t clarityRadius  = 2;     // 1 .. 4
  float   clarityMidtone = 1.2f;

  // --- paper normalisation ---
  uint8_t paperMode     = PAPER_OFF;
  float   paperStrength = 1.0f;   // 0 .. 1
};

// Everything imgprocRow() needs, resolved once per frame.
struct ImgProcPlan {
  bool    any = false;          // false => imgprocRow() is a no-op, skip it

  uint8_t expoLut[256];
  uint8_t toneLut[256];
  bool    doExposure = false;
  bool    doTone     = false;

  bool    doSat = false;
  float   satMul = 1.0f;

  // --- DRC ---
  uint8_t drcMode = DRC_OFF;
  bool    drcAccurate = false;
  float   drcStrength = 1.0f;
  float   drcTargetLo = 0.0f;   // palette black, in the working domain
  float   drcTargetRange = 1.0f;
  float   drcSrcLo = 0.0f;      // source percentiles, in the working domain
  float   drcSrcRange = 1.0f;

  // --- level ---
  uint8_t levelMode = LVL_OFF;
  uint8_t lvlLut[3][256];
  float   lvlBlackY = 0.0f, lvlRangeY = 255.0f;

  // --- white preservation ---
  bool    presWhite = false;
  float   presLuma = 255.0f;    // source luma at the configured percentile
  float   presMaxSat = 0.18f;
  float   presTargetLuma = 255.0f;
  uint8_t presRgb[3] = {255, 255, 255};
};

// Fill `out` with the preset's values. PROC_CUSTOM leaves everything neutral.
// Does not touch `out->preset`.
//
// epdoptimize's presets also carry a colour-matching mode and an
// error-diffusion kernel, which live in DitherCfg rather than here; pass
// non-null pointers to receive them. Both are optional.
void imgprocPreset(uint8_t preset, ProcSettings* out, uint8_t* colorMatching = nullptr,
                   uint8_t* edMatrix = nullptr);
const char* procPresetName(uint8_t preset);
const char* toneModeName(uint8_t m);
const char* drcModeName(uint8_t m);
const char* levelModeName(uint8_t m);
const char* paperModeName(uint8_t m);

// True when the source pass would do anything, so the caller can skip the
// whole sweep (and its allocation) in the common case.
bool imgprocSourcePassNeeded(const ProcSettings& ps, float gamma);

// Apply gamma, paper normalisation and clarity to the decoded RGB565 source,
// in place. `outScale` is the render scale factor (output pixels per source
// pixel); the clarity radius is DIVIDED by it, because a radius of R output
// pixels spans R/outScale source pixels. That keeps the slider meaning the
// same thing whatever the source resolution, and matches what the references
// produce on a canvas that has already been resized. Returns false if it had
// to skip clarity because the sliding-window scratch could not be allocated --
// the caller should log that, not fail.
bool imgprocSourcePass(uint16_t* px, int w, int h, const ProcSettings& ps, float gamma,
                       float outScale);

// Resolve the lookup tables and the "auto" modes. `rect` is the region of the
// source that will actually end up on the panel, in source pixel coordinates,
// so that a cover-mode crop does not let the discarded edges skew the
// percentiles. Pass the whole image for contain mode.
struct ImgProcRect { int x0, y0, x1, y1; };   // inclusive
void imgprocAnalyze(const uint16_t* px, int w, int h, ImgProcRect rect,
                    const E6Palette& pal, const ProcSettings& ps, ImgProcPlan* out);

// Apply every pointwise stage to one row of `n` RGB888 pixels, in place.
void imgprocRow(const ImgProcPlan& plan, uint8_t* rgb888, int n);
