#pragma once
#include <Arduino.h>
#include "palette.h"

// =============================================================================
//  Streaming 6-colour (E Ink Spectra 6) ditherer.
//
//  WHY THIS EXISTS
//  ---------------
//  The official Seeed pipeline (Seeed_GFX examples, dither.cpp) dithers a whole
//  image at once: it needs a width*height index buffer AND, for error
//  diffusion, a width*height*3*int16 error buffer. At the E1004's full panel
//  size of 1200x1600 that error buffer alone is ~11 MB, so on an 8 MB module
//  `dither_image()` fails the allocation and silently degrades to no dithering.
//  The official example's own comments acknowledge this and default to Bayer8.
//
//  This implementation instead keeps only THREE rows of error, which is enough
//  for every kernel here (the deepest, Stucki/Jarvis/Sierra-3, reaches dy=+2).
//  That is ~29 kB instead of 11 MB, which means full-resolution error diffusion
//  becomes practical and the picture quality is visibly better than ordered
//  dithering. Widening the kernel set did not change that budget.
//
//  WHAT IT MATCHES, AND WHERE IT DELIBERATELY DOES NOT
//  ---------------------------------------------------
//  The kernels, the Bayer construction and the three colour-distance models are
//  ports of paperlesspaper/epdoptimize (src/dither/). Quantisation matches
//  against a CALIBRATED palette and emits the device nibble code -- see the
//  long comment in palette.h for why that split is the whole point.
//
//  Three deliberate deviations, all of them documented at their call sites:
//
//    * Error is kept as a separate signed delta and clamped only when the pixel
//      is quantised. Seeed's code, and epdoptimize's, write "value + error"
//      back into the pixel buffer clamped to 0..255 at every accumulation,
//      which throws error away at saturation. Ours is the textbook behaviour
//      and loses less highlight/shadow detail; output is therefore very close
//      to, but not bit-identical with, either reference.
//    * Ordered dithering uses a threshold centred on zero. epdoptimize adds a
//      positive-only `factor * 64`, which brightens the whole image.
//    * `DITHER_RANDOM` adds noise and then matches the palette. epdoptimize's
//      `random` mode hard-codes each channel to 0 or 255 and ignores the
//      palette entirely, which is meaningless for a six-ink panel.
// =============================================================================

enum DitherType {
  DITHER_ERROR_DIFFUSION = 0,  // one of the EdMatrix kernels below
  DITHER_ORDERED,              // Bayer threshold matrix, no error buffer at all
  DITHER_RANDOM,               // deterministic per-pixel noise, then match
  DITHER_QUANTIZE_ONLY,        // nearest colour, no dithering
  DITHER_TYPES
};

// The full set from epdoptimize/src/dither/data/diffusion-maps.ts. The four
// that esp32-photoframe ships on-device (floydSteinberg, stucki, burkes,
// sierra3) are all here with identical weights, so output is comparable.
enum EdMatrix {
  ED_FLOYD_STEINBERG = 0,
  ED_FALSE_FLOYD_STEINBERG,
  ED_ATKINSON,
  ED_JARVIS,            // == jarvisJudiceNinke
  ED_STUCKI,
  ED_BURKES,
  ED_SIERRA3,
  ED_SIERRA2,
  ED_SIERRA2_4A,
  ED_FAN,
  ED_SHIAU_FAN,
  ED_SHIAU_FAN2,
  ED_MATRICES
};

enum ColorMatching {
  CM_RGB = 0,  // squared Euclidean in RGB
  CM_LAB,      // CIE76 dE on CIELAB
  CM_CHROMA    // RGB plus epdoptimize's saturation and hue penalties
};

struct DitherCfg {
  DitherType    type       = DITHER_ERROR_DIFFUSION;
  EdMatrix      matrix     = ED_FLOYD_STEINBERG;
  ColorMatching matching   = CM_RGB;
  bool          serpentine = false;
  uint8_t       bayerSize  = 8;    // 2, 4, 8 or 16
  uint8_t       orderedStrength = 64;  // peak-to-peak threshold spread
};

// Raw 4bpp nibble codes the T133A01 controller expects, in PaletteSlot order.
#define E6_WHITE  0x0
#define E6_GREEN  0x2
#define E6_RED    0x6
#define E6_YELLOW 0xB
#define E6_BLUE   0xD
#define E6_BLACK  0xF

class E6Ditherer {
 public:
  // `width` is the number of pixels per row that will be fed in. `pal` must
  // outlive the ditherer. Returns false only if the (small) error buffer could
  // not be allocated, in which case it degrades to ordered dithering.
  bool begin(int width, const E6Palette& pal, const DitherCfg& cfg);
  void end();

  // Restart at row 0 without reallocating.
  void reset();

  // Quantise one row: `rgb888` is width*3 bytes, `outCodes` receives width
  // bytes of E6_* nibble codes. Rows MUST be fed top to bottom, starting at 0.
  void row(const uint8_t* rgb888, uint8_t* outCodes);

  int width() const { return w_; }

 private:
  int        w_ = 0;
  DitherCfg  cfg_;
  E6Palette  pal_{};
  int        y_   = 0;
  int16_t*   err_ = nullptr;   // 3 rows x w_ x 3 channels, signed deltas
  float      palLab_[PAL_SLOTS][3];
  float      palSat_[PAL_SLOTS];
  float      palHue_[PAL_SLOTS];
  uint8_t    bayer_[256];      // up to 16x16, scaled to 0..255
  uint8_t    bayerMask_ = 7;   // bayerSize - 1

  int16_t* errRow(int row) { return err_ + (size_t)(row % 3) * w_ * 3; }

  // `sr/sg/sb` is the pixel before error accumulation; CM_CHROMA weighs its
  // saturation and hue, exactly as findClosestPaletteColor() does with its
  // separate `sourcePixel` argument.
  int nearest(int r, int g, int b, int sr, int sg, int sb) const;
};

// RGB of a nibble code in `pal`, for diagnostics and the admin-page preview.
// Falls back to the palette's white for an unknown code.
void e6CodeToRgb(const E6Palette& pal, uint8_t code, uint8_t* r, uint8_t* g, uint8_t* b);

// Nibble code for a palette slot.
uint8_t e6SlotCode(int slot);

// Sum of a kernel's diffusion weights. Every kernel here conserves the whole
// error (1.0) except Atkinson, which deliberately discards a quarter of it for
// extra contrast. Exposed because a typo in one numerator is invisible to any
// statistical check of the output -- a flat field settles to an attractor close
// to the input even when a quarter of the error is thrown away.
float e6KernelWeightSum(uint8_t matrix);

const char* ditherTypeName(uint8_t t);
const char* edMatrixName(uint8_t m);
const char* colorMatchName(uint8_t m);
