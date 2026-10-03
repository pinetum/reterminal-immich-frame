#include "palette.h"

namespace {

// Slot order throughout: white, green, red, yellow, blue, black.
const E6Palette kPalettes[PALETTE_IDS] = {
    // PAL_SEEED -- verbatim from Seeed_GFX dither.cpp. Theoretical, not measured.
    {{{255, 255, 255}, {29, 185, 84}, {229, 57, 53}, {255, 216, 0}, {0, 76, 255}, {0, 0, 0}}},
    // PAL_SPECTRA6 -- epdoptimize "spectra6":
    // #B9C7C9 #35563A #62201E #C1BB1E #233F8E #1F2226
    {{{0xB9, 0xC7, 0xC9},
      {0x35, 0x56, 0x3A},
      {0x62, 0x20, 0x1E},
      {0xC1, 0xBB, 0x1E},
      {0x23, 0x3F, 0x8E},
      {0x1F, 0x22, 0x26}}},
    // PAL_SPECTRA6_LEGACY -- epdoptimize "spectra6legacy":
    // #e8e8e8 #125f20 #b21318 #efde44 #2157ba #191E21
    {{{0xE8, 0xE8, 0xE8},
      {0x12, 0x5F, 0x20},
      {0xB2, 0x13, 0x18},
      {0xEF, 0xDE, 0x44},
      {0x21, 0x57, 0xBA},
      {0x19, 0x1E, 0x21}}},
    // PAL_SPECTRA6_BOEBER -- epdoptimize "spectra6-boeber":
    // #d6d6d6 #067406 #ea4843 #dbd529 #416ce1 #1f2226
    {{{0xD6, 0xD6, 0xD6},
      {0x06, 0x74, 0x06},
      {0xEA, 0x48, 0x43},
      {0xDB, 0xD5, 0x29},
      {0x41, 0x6C, 0xE1},
      {0x1F, 0x22, 0x26}}},
    // PAL_AITJCIZE -- epdoptimize "aitjcize-spectra6", identical to
    // color_palette_get_defaults() in esp32-photoframe/main/color_palette.c:
    // #BEC8C8 #27663C #871300 #CDCA00 #05409E #020202
    {{{0xBE, 0xC8, 0xC8},
      {0x27, 0x66, 0x3C},
      {0x87, 0x13, 0x00},
      {0xCD, 0xCA, 0x00},
      {0x05, 0x40, 0x9E},
      {0x02, 0x02, 0x02}}},
    // PAL_CUSTOM -- never read from here; the seed for a fresh custom palette.
    {{{0xB9, 0xC7, 0xC9},
      {0x35, 0x56, 0x3A},
      {0x62, 0x20, 0x1E},
      {0xC1, 0xBB, 0x1E},
      {0x23, 0x3F, 0x8E},
      {0x1F, 0x22, 0x26}}},
};

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

const E6Palette& paletteBuiltin(uint8_t id) {
  if (id >= PALETTE_IDS) id = PAL_SEEED;
  return kPalettes[id];
}

const char* paletteIdName(uint8_t id) {
  switch (id) {
    case PAL_SEEED:            return "seeed";
    case PAL_SPECTRA6:         return "spectra6";
    case PAL_SPECTRA6_LEGACY:  return "spectra6legacy";
    case PAL_SPECTRA6_BOEBER:  return "spectra6boeber";
    case PAL_AITJCIZE:         return "aitjcize";
    case PAL_CUSTOM:           return "custom";
  }
  return "seeed";
}

bool paletteHexParse(const char* hex, uint8_t rgb[3]) {
  if (!hex) return false;
  while (*hex == ' ' || *hex == '#') ++hex;

  int n = 0;
  while (hex[n] && hexNibble(hex[n]) >= 0) ++n;
  // Trailing whitespace is fine, anything else is not.
  for (int i = n; hex[i]; ++i)
    if (hex[i] != ' ') return false;

  if (n == 6) {
    for (int i = 0; i < 3; ++i)
      rgb[i] = (uint8_t)((hexNibble(hex[i * 2]) << 4) | hexNibble(hex[i * 2 + 1]));
    return true;
  }
  if (n == 3) {
    // #abc means #aabbcc, the same shorthand hexToRgb() accepts in epdoptimize.
    for (int i = 0; i < 3; ++i) {
      const int v = hexNibble(hex[i]);
      rgb[i] = (uint8_t)((v << 4) | v);
    }
    return true;
  }
  return false;
}

void paletteHexFormat(const uint8_t rgb[3], char* out) {
  static const char* kDigits = "0123456789abcdef";
  out[0] = '#';
  for (int i = 0; i < 3; ++i) {
    out[1 + i * 2] = kDigits[rgb[i] >> 4];
    out[2 + i * 2] = kDigits[rgb[i] & 0xF];
  }
  out[7] = '\0';
}

float paletteSlotLuma(const E6Palette& p, int slot) {
  return 0.2126f * p.rgb[slot][0] + 0.7152f * p.rgb[slot][1] + 0.0722f * p.rgb[slot][2];
}

int paletteDarkest(const E6Palette& p) {
  int best = 0;
  float bestY = paletteSlotLuma(p, 0);
  for (int i = 1; i < PAL_SLOTS; ++i) {
    const float y = paletteSlotLuma(p, i);
    if (y < bestY) { bestY = y; best = i; }
  }
  return best;
}

int paletteLightest(const E6Palette& p) {
  int best = 0;
  float bestY = paletteSlotLuma(p, 0);
  for (int i = 1; i < PAL_SLOTS; ++i) {
    const float y = paletteSlotLuma(p, i);
    if (y > bestY) { bestY = y; best = i; }
  }
  return best;
}
