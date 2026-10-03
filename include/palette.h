#pragma once
#include <Arduino.h>

// =============================================================================
//  Calibrated Spectra 6 palettes.
//
//  WHY THIS EXISTS
//  ---------------
//  An E Ink Spectra 6 panel drives six inks, and the controller addresses them
//  by nibble code (see E6_* below). Those codes are often described by the
//  saturated RGB you would *like* them to be -- white 0xFFFFFF, red 0xFF0000 --
//  and that is what Seeed's dither.cpp quantises against. The panel does not
//  produce those colours. Measured on real hardware, white is about #B9C7C9 and
//  green about #35563A; aitjcize's measurements put white 30% and green 73%
//  darker than theoretical (esp32-photoframe, docs/MEASURED_PALETTE.md).
//
//  So a nearest-colour search against the saturated values answers the wrong
//  question, and -- worse for error diffusion -- the error fed to the
//  neighbours is computed against a colour the panel will never show, so the
//  neighbours "correct" for a mistake that was never made. The fix, which
//  paperlesspaper/epdoptimize and aitjcize/esp32-photoframe arrive at
//  independently, is a three-way split:
//
//      match and diffuse against the CALIBRATED colour,
//      emit the DEVICE colour.
//
//  In epdoptimize that second step is a separate `replaceColors()` pass over
//  the finished image, because it works in RGB canvases. We do not need it: our
//  ditherer already emits nibble codes, and a nibble code *is* the device
//  colour. The calibrated RGB lives here; the codes stay in e6_dither.h.
//
//  Every table below is quoted from one of the two reference projects so the
//  output is comparable with their tooling. None of them is "right" for every
//  panel -- Spectra 6 batches differ -- which is why PAL_CUSTOM exists.
// =============================================================================

// Palette entries are in a fixed order that matches kCode[] in e6_dither.cpp.
// Do not reorder: the index is what the ditherer indexes the nibble table with.
enum PaletteSlot {
  PAL_WHITE = 0,
  PAL_GREEN,
  PAL_RED,
  PAL_YELLOW,
  PAL_BLUE,
  PAL_BLACK,
  PAL_SLOTS
};

struct E6Palette {
  uint8_t rgb[PAL_SLOTS][3];
};

enum PaletteId {
  // The saturated values from Seeed's dither.cpp, i.e. what this firmware
  // quantised against before calibrated palettes existed. Kept as the default
  // so an existing device renders identically after an update.
  PAL_SEEED = 0,
  PAL_SPECTRA6,         // epdoptimize "spectra6" -- its recommended calibration
  PAL_SPECTRA6_LEGACY,  // epdoptimize "spectra6legacy"
  PAL_SPECTRA6_BOEBER,  // epdoptimize "spectra6-boeber"
  PAL_AITJCIZE,         // epdoptimize "aitjcize-spectra6" == esp32-photoframe defaults
  PAL_CUSTOM,           // six hex values the user measured, stored in NVS
  PALETTE_IDS
};

// Built-in table for `id`. PAL_CUSTOM returns PAL_SPECTRA6, which is what a
// fresh custom palette is seeded from.
const E6Palette& paletteBuiltin(uint8_t id);

const char* paletteIdName(uint8_t id);

// "#RRGGBB" / "RRGGBB" / "#RGB" -> rgb[3]. Returns false and leaves `rgb`
// untouched on anything it cannot parse.
bool paletteHexParse(const char* hex, uint8_t rgb[3]);

// rgb[3] -> "#rrggbb". `out` needs 8 bytes.
void paletteHexFormat(const uint8_t rgb[3], char* out);

// Relative luminance (Rec. 709) of a palette slot, used to find the black and
// white endpoints for dynamic-range compression.
float paletteSlotLuma(const E6Palette& p, int slot);

// Indices of the darkest and lightest entries. Not hardcoded to PAL_BLACK and
// PAL_WHITE because a user-supplied custom palette need not be sane, and
// getPaletteEndpoints() in epdoptimize derives them the same way.
int paletteDarkest(const E6Palette& p);
int paletteLightest(const E6Palette& p);
