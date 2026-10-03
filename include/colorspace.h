#pragma once
#include <Arduino.h>
#include <math.h>

// =============================================================================
//  Colour-space helpers shared by the ditherer (palette matching) and the
//  pre-dither processor (tone mapping, dynamic-range compression).
//
//  Everything here is table-driven. The reference implementations call powf()
//  and cbrtf() per pixel per channel, which is fine in a browser and is not
//  fine at 1200x1600 on a 240 MHz Xtensa: a full-frame LAB round trip is
//  5.8 M transcendental calls. esp32-photoframe makes the same move -- see
//  init_gamma_luts() and linear_to_srgb_lut[4096] in main/image_processor.c --
//  and we extend it to the cube root that CIELAB needs.
//
//  Accuracy of the cbrt table: 1024 intervals with linear interpolation gives
//  well under 0.01 in L*, which is two orders of magnitude below the 1.0 that
//  is just-noticeable. The values the tables feed are then quantised to one of
//  six inks, so this is not the weak link in the chain.
// =============================================================================

// Builds the tables. Idempotent and cheap to call again; every entry point
// below assumes it has already run.
void csInit();

// --- sRGB <-> linear light ---------------------------------------------------
extern float   g_csToLinear[256];
#define CS_FROM_LINEAR_BINS 4096
extern uint8_t g_csFromLinear[CS_FROM_LINEAR_BINS];

inline float csSrgbToLinear(uint8_t v) { return g_csToLinear[v]; }

inline uint8_t csLinearToSrgb(float lin) {
  if (lin <= 0.0f) return 0;
  if (lin >= 1.0f) return 255;
  return g_csFromLinear[(int)(lin * (CS_FROM_LINEAR_BINS - 1) + 0.5f)];
}

// --- CIELAB ------------------------------------------------------------------
#define CS_PIVOT_BINS 1024
extern float g_csPivot[CS_PIVOT_BINS + 1];   // labForwardPivot() over t in [0,1]

// t > 0.008856 ? cbrt(t) : 7.787*t + 16/116, the CIELAB forward pivot.
inline float csPivot(float t) {
  if (t <= 0.0f) return 16.0f / 116.0f;
  if (t >= 1.0f) return cbrtf(t);            // only reachable above the white point
  const float f = t * CS_PIVOT_BINS;
  const int   i = (int)f;
  const float frac = f - i;
  return g_csPivot[i] + (g_csPivot[i + 1] - g_csPivot[i]) * frac;
}

// Rec. 709 relative luminance of a gamma-encoded sRGB triple. This is the
// quantity epdoptimize calls luma709() and uses for percentiles, saturation
// masks and the fast dynamic-range path. Note it is deliberately computed on
// the gamma-encoded values -- it is a perceptual weighting, not a luminance.
inline float csLuma709(int r, int g, int b) {
  return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// True linear-light relative luminance Y in [0,1]. This is what dynamic-range
// compression scales, following cdr_apply_row() in esp32-photoframe.
inline float csLinearY(int r, int g, int b) {
  return 0.2126729f * g_csToLinear[r] + 0.7151522f * g_csToLinear[g] +
         0.0721750f * g_csToLinear[b];
}

// CIE L* (0..100) of a linear-light Y.
inline float csLstarFromY(float y) {
  return y > 0.008856f ? 116.0f * csPivot(y) - 16.0f : 903.3f * y;
}

// Full CIELAB (D65), matching rgbToLab() in epdoptimize/src/dither/processing.ts.
void csRgbToLab(int r, int g, int b, float lab[3]);

// Squared CIE76 colour difference. Squared because every caller only compares
// distances, and the square root would be 1.9 M wasted calls per frame.
inline float csDeltaE2(const float a[3], const float b[3]) {
  const float dl = a[0] - b[0], da = a[1] - b[1], db = a[2] - b[2];
  return dl * dl + da * da + db * db;
}

// --- HSV-ish helpers ---------------------------------------------------------
// getSaturation() in both references: max == 0 ? 0 : (max - min) / max.
inline float csSaturation(int r, int g, int b) {
  const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
  const int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  return mx == 0 ? 0.0f : (float)(mx - mn) / (float)mx;
}

// Hue in degrees, 0..360. Returns 0 for neutrals.
float csHue(int r, int g, int b);

// Shortest distance between two hues, 0..180.
inline float csHueDistance(float a, float b) {
  float d = fmodf(fabsf(a - b), 360.0f);
  return d > 180.0f ? 360.0f - d : d;
}

inline int csClamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

inline float csClampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// smoothstep(edge0, edge1, x), used by the chroma-protection masks.
inline float csSmoothstep(float e0, float e1, float v) {
  if (e1 <= e0) return v >= e1 ? 1.0f : 0.0f;
  const float x = csClampf((v - e0) / (e1 - e0), 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

// normalize(v, min, max) clamped to 0..1, epdoptimize's mask primitive.
inline float csNormalize(float v, float lo, float hi) {
  if (hi <= lo) return v >= hi ? 1.0f : 0.0f;
  return csClampf((v - lo) / (hi - lo), 0.0f, 1.0f);
}
