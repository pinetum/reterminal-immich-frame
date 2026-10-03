// The calibrated palette tables and the hex parsing the admin page feeds them.
//
// The built-in values are checked against the literal hex strings quoted in the
// reference projects, because a transposed digit in one of these tables is both
// invisible (the picture just looks slightly wrong) and the single most
// damaging mistake available here: everything downstream -- nearest-colour
// matching, the diffused error, the dynamic-range endpoints, the preview --
// is computed from them.
#include "palette.h"
#include "colorspace.h"
#include <cstdio>
#include <cstring>

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf("  FAIL: %s\n", m); fails++; } } while (0)

static void expectPalette(uint8_t id, const char* name, const char* white, const char* green,
                          const char* red, const char* yellow, const char* blue,
                          const char* black) {
  const char* want[PAL_SLOTS] = {white, green, red, yellow, blue, black};
  const E6Palette& p = paletteBuiltin(id);
  char got[8];
  printf("   %-15s", name);
  bool ok = true;
  for (int i = 0; i < PAL_SLOTS; ++i) {
    paletteHexFormat(p.rgb[i], got);
    printf(" %s", got);
    uint8_t ref[3];
    if (!paletteHexParse(want[i], ref) || memcmp(ref, p.rgb[i], 3) != 0) ok = false;
  }
  printf("  %s\n", ok ? "" : "<-- MISMATCH");
  CHECK(ok, "built-in palette does not match the reference values");
  CHECK(strcmp(paletteIdName(id), name) == 0, "palette name");
}

static void testBuiltins() {
  printf("-- built-in tables vs the reference projects\n");
  // Seeed_GFX dither.cpp (theoretical, not measured).
  expectPalette(PAL_SEEED, "seeed", "#ffffff", "#1db954", "#e53935", "#ffd800", "#004cff",
                "#000000");
  // epdoptimize src/dither/data/default-palettes.json, "spectra6".
  expectPalette(PAL_SPECTRA6, "spectra6", "#B9C7C9", "#35563A", "#62201E", "#C1BB1E", "#233F8E",
                "#1F2226");
  // ... "spectra6legacy".
  expectPalette(PAL_SPECTRA6_LEGACY, "spectra6legacy", "#e8e8e8", "#125f20", "#b21318", "#efde44",
                "#2157ba", "#191E21");
  // ... "spectra6-boeber".
  expectPalette(PAL_SPECTRA6_BOEBER, "spectra6boeber", "#d6d6d6", "#067406", "#ea4843", "#dbd529",
                "#416ce1", "#1f2226");
  // ... "aitjcize-spectra6", which is also color_palette_get_defaults() in
  // esp32-photoframe/main/color_palette.c: {190,200,200} {39,102,60}
  // {135,19,0} {205,202,0} {5,64,158} {2,2,2}.
  expectPalette(PAL_AITJCIZE, "aitjcize", "#BEC8C8", "#27663C", "#871300", "#CDCA00", "#05409E",
                "#020202");

  // A fresh custom palette is seeded from spectra6 rather than left at zero, so
  // the admin page's six fields open on something sensible.
  const E6Palette& custom = paletteBuiltin(PAL_CUSTOM);
  CHECK(memcmp(custom.rgb, paletteBuiltin(PAL_SPECTRA6).rgb, sizeof(custom.rgb)) == 0,
        "the custom palette must be seeded from spectra6");

  // An out-of-range id must not index past the table.
  CHECK(memcmp(paletteBuiltin(PALETTE_IDS).rgb, paletteBuiltin(PAL_SEEED).rgb,
               sizeof(custom.rgb)) == 0,
        "an invalid palette id must fall back to the default");
  CHECK(strcmp(paletteIdName(99), "seeed") == 0, "an invalid id must still name something");
}

static void testHex() {
  printf("\n-- hex parsing (the admin page's six text fields land here)\n");
  struct Good { const char* in; uint8_t r, g, b; };
  const Good good[] = {
      {"#B9C7C9", 0xB9, 0xC7, 0xC9},
      {"b9c7c9", 0xB9, 0xC7, 0xC9},      // no hash
      {"#B9c7C9", 0xB9, 0xC7, 0xC9},     // mixed case
      {"  #000000", 0, 0, 0},            // leading space
      {"#ffffff  ", 255, 255, 255},      // trailing space
      {"#abc", 0xAA, 0xBB, 0xCC},        // the shorthand hexToRgb() accepts
      {"fff", 255, 255, 255},
  };
  for (const auto& g : good) {
    uint8_t rgb[3] = {1, 2, 3};
    const bool ok = paletteHexParse(g.in, rgb);
    printf("   %-12s -> %s %02x%02x%02x\n", g.in, ok ? "ok  " : "FAIL", rgb[0], rgb[1], rgb[2]);
    CHECK(ok, "valid hex must parse");
    CHECK(rgb[0] == g.r && rgb[1] == g.g && rgb[2] == g.b, "parsed value");
  }

  const char* bad[] = {"", "#", "#12", "#12345", "#1234567", "#gggggg", "#12 34 56", "zz",
                       "#12345g"};
  for (const char* in : bad) {
    uint8_t rgb[3] = {11, 22, 33};
    const bool ok = paletteHexParse(in, rgb);
    printf("   %-12s -> %s\n", *in ? in : "(empty)", ok ? "PARSED (should not)" : "rejected");
    CHECK(!ok, "invalid hex must be rejected");
    CHECK(rgb[0] == 11 && rgb[1] == 22 && rgb[2] == 33,
          "a rejected string must leave the colour untouched");
  }
  {
    uint8_t rgb[3] = {11, 22, 33};
    CHECK(!paletteHexParse(nullptr, rgb), "a null string must be rejected, not dereferenced");
    printf("   %-12s -> rejected\n", "(null)");
  }

  // Round trip every byte triple the formatter can produce.
  int bad_rt = 0;
  for (int i = 0; i < 256; ++i) {
    const uint8_t in[3] = {(uint8_t)i, (uint8_t)(255 - i), (uint8_t)((i * 7) & 0xFF)};
    char hex[8];
    uint8_t out[3];
    paletteHexFormat(in, hex);
    if (!paletteHexParse(hex, out) || memcmp(in, out, 3) != 0) bad_rt++;
  }
  printf("   format/parse round trip: %d of 256 failed\n", bad_rt);
  CHECK(bad_rt == 0, "format then parse must be the identity");
}

static void testEndpoints() {
  printf("\n-- endpoints: what dynamic-range compression compresses towards\n");
  csInit();
  struct { uint8_t id; const char* n; } pals[] = {
      {PAL_SEEED, "seeed"}, {PAL_SPECTRA6, "spectra6"}, {PAL_SPECTRA6_LEGACY, "legacy"},
      {PAL_SPECTRA6_BOEBER, "boeber"}, {PAL_AITJCIZE, "aitjcize"}};

  for (auto& p : pals) {
    const E6Palette& pal = paletteBuiltin(p.id);
    const int dark = paletteDarkest(pal), light = paletteLightest(pal);
    printf("   %-9s darkest=%d (Y %6.1f)  lightest=%d (Y %6.1f)  white reflectance %.0f%%\n", p.n,
           dark, paletteSlotLuma(pal, dark), light, paletteSlotLuma(pal, light),
           100.0 * csLinearY(pal.rgb[light][0], pal.rgb[light][1], pal.rgb[light][2]));
    // Derived by luma rather than hardcoded, because a user-supplied custom
    // palette need not put black in the black slot. For every built-in they
    // do coincide, and that is worth asserting.
    CHECK(dark == PAL_BLACK, "the darkest built-in entry should be the black ink");
    CHECK(light == PAL_WHITE, "the lightest built-in entry should be the white ink");
  }

  // The headline finding of esp32-photoframe/docs/MEASURED_PALETTE.md: a real
  // panel's white is far from paper white. If a future edit flattened the
  // calibrated tables back towards the theoretical ones, this is what would
  // notice.
  const E6Palette& cal = paletteBuiltin(PAL_SPECTRA6);
  const float calWhite = csLinearY(cal.rgb[PAL_WHITE][0], cal.rgb[PAL_WHITE][1],
                                   cal.rgb[PAL_WHITE][2]);
  printf("   calibrated white sits at %.0f%% of theoretical white's luminance\n",
         100.0 * calWhite);
  CHECK(calWhite < 0.75f, "a calibrated palette's white must be well below 1.0");
  CHECK(calWhite > 0.30f, "...but not so dark that the table is obviously wrong");

  // A deliberately perverse custom palette: the endpoints must still be found
  // by luminance, not by slot.
  {
    E6Palette odd = paletteBuiltin(PAL_SPECTRA6);
    for (int c = 0; c < 3; ++c) {
      odd.rgb[PAL_WHITE][c] = 0;      // "white" slot holds black
      odd.rgb[PAL_BLACK][c] = 255;    // "black" slot holds white
    }
    printf("   inverted custom palette: darkest=%d lightest=%d\n", paletteDarkest(odd),
           paletteLightest(odd));
    CHECK(paletteDarkest(odd) == PAL_WHITE, "endpoints must come from luminance, not slot order");
    CHECK(paletteLightest(odd) == PAL_BLACK, "endpoints must come from luminance, not slot order");
  }
}

static void testNvsBlob() {
  printf("\n-- the NVS blob is exactly the 18 bytes settings.cpp stores\n");
  // settingsSave() does putBytes("palCustom", p.rgb, sizeof(E6Palette::rgb)).
  // If E6Palette ever grew a member before rgb, or rgb stopped being tightly
  // packed, every stored custom palette would silently decode as garbage.
  printf("   sizeof(E6Palette)=%zu  sizeof(rgb)=%zu\n", sizeof(E6Palette),
         sizeof(E6Palette::rgb));
  CHECK(sizeof(E6Palette::rgb) == 18, "the palette blob must be 6 slots x 3 bytes");
  CHECK(sizeof(E6Palette) == 18, "E6Palette must be nothing but its rgb array");

  E6Palette a = paletteBuiltin(PAL_AITJCIZE), b{};
  uint8_t blob[18];
  memcpy(blob, a.rgb, sizeof(blob));
  memcpy(b.rgb, blob, sizeof(blob));
  CHECK(memcmp(a.rgb, b.rgb, sizeof(a.rgb)) == 0, "blob round trip");

  // Byte order: slot 0 red must be the first byte, or the admin page's fields
  // would come back permuted.
  CHECK(blob[0] == a.rgb[PAL_WHITE][0] && blob[17] == a.rgb[PAL_BLACK][2],
        "the blob must be in slot-major order");
}

int main() {
  testBuiltins();
  testHex();
  testEndpoints();
  testNvsBlob();
  printf(fails ? "\n%d FAILED\n" : "\nall checks passed\n", fails);
  return fails ? 1 : 0;
}
