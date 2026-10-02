#pragma once
#include <Arduino.h>

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
//  This implementation instead keeps only THREE rows of error (the deepest
//  kernel we support, Jarvis/Atkinson, reaches dy=+2). That is ~29 kB instead
//  of 11 MB, which means full-resolution Floyd-Steinberg becomes practical and
//  the picture quality is visibly better than ordered dithering.
//
//  Palette values, nibble codes, the Bayer matrix and the three diffusion
//  kernels are taken verbatim from the official Seeed dither.cpp so colours
//  match what the Seeed tooling produces.
//
//  One deliberate deviation: the official code stores "pixel value + error"
//  clamped to 0..255 at every accumulation step, which throws away error at
//  saturation. We keep the error as a separate signed delta and clamp only when
//  the pixel is quantised. That is the textbook-correct behaviour and loses
//  less detail in highlights/shadows; output is therefore very close to, but
//  not bit-identical with, Seeed's browser tool.
// =============================================================================

enum DitherMethod {
  DITHER_NONE = 0,   // nearest colour, no dithering
  DITHER_BAYER8,     // 8x8 ordered Bayer (no error buffer at all)
  DITHER_FS,         // Floyd-Steinberg
  DITHER_JARVIS,     // Jarvis-Judice-Ninke
  DITHER_ATKINSON,   // Atkinson
};

// Raw 4bpp nibble codes the T133A01 controller expects.
#define E6_WHITE  0x0
#define E6_GREEN  0x2
#define E6_RED    0x6
#define E6_YELLOW 0xB
#define E6_BLUE   0xD
#define E6_BLACK  0xF

class E6Ditherer {
 public:
  // `width` is the number of pixels per row that will be fed in. Returns false
  // only if the (small) error buffer could not be allocated.
  bool begin(int width, DitherMethod method, float gamma);
  void end();

  // Restart at row 0 without reallocating.
  void reset();

  // Quantise one row: `rgb888` is width*3 bytes, `outCodes` receives width
  // bytes of E6_* nibble codes. Rows MUST be fed top to bottom, starting at 0.
  void row(const uint8_t* rgb888, uint8_t* outCodes);

  int width() const { return w_; }

 private:
  int          w_      = 0;
  DitherMethod m_      = DITHER_FS;
  int          y_      = 0;
  int16_t*     err_    = nullptr;   // 3 rows x w_ x 3 channels, signed deltas
  uint8_t      gamma_[256];
  bool         gammaIdentity_ = true;

  int16_t* errRow(int row) { return err_ + (size_t)(row % 3) * w_ * 3; }
};

// Expose the palette for callers that need the RGB of a code (diagnostics,
// preview rendering).
void e6CodeToRgb(uint8_t code, uint8_t* r, uint8_t* g, uint8_t* b);
