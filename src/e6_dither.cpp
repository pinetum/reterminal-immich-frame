#include "e6_dither.h"
#include <math.h>

namespace {

struct Rgb { uint8_t r, g, b; };

// Spectra 6 palette, verbatim from Seeed_GFX's dither.cpp so our output matches
// the colours their tooling produces.
const Rgb kPal[6] = {
    {255, 255, 255},  // 0: WHITE  -> 0x0
    { 29, 185,  84},  // 1: GREEN  -> 0x2
    {229,  57,  53},  // 2: RED    -> 0x6
    {255, 216,   0},  // 3: YELLOW -> 0xB
    {  0,  76, 255},  // 4: BLUE   -> 0xD
    {  0,   0,   0},  // 5: BLACK  -> 0xF
};
const uint8_t kCode[6] = {E6_WHITE, E6_GREEN, E6_RED, E6_YELLOW, E6_BLUE, E6_BLACK};

const uint8_t kBayer8[64] = {
     0, 48, 12, 60,  3, 51, 15, 63,
    32, 16, 44, 28, 35, 19, 47, 31,
     8, 56,  4, 52, 11, 59,  7, 55,
    40, 24, 36, 20, 43, 27, 39, 23,
     2, 50, 14, 62,  1, 49, 13, 61,
    34, 18, 46, 30, 33, 17, 45, 29,
    10, 58,  6, 54,  9, 57,  5, 53,
    42, 26, 38, 22, 41, 25, 37, 21,
};

struct Tap { int8_t dx, dy; int16_t num, den; };

const Tap kFS[] = {
    { 1, 0, 7, 16}, {-1, 1, 3, 16}, { 0, 1, 5, 16}, { 1, 1, 1, 16},
};
const Tap kJarvis[] = {
    { 1, 0, 7, 48}, { 2, 0, 5, 48},
    {-2, 1, 3, 48}, {-1, 1, 5, 48}, { 0, 1, 7, 48}, { 1, 1, 5, 48}, { 2, 1, 3, 48},
    {-2, 2, 1, 48}, {-1, 2, 3, 48}, { 0, 2, 5, 48}, { 1, 2, 3, 48}, { 2, 2, 1, 48},
};
const Tap kAtkinson[] = {
    { 1, 0, 1, 8}, { 2, 0, 1, 8},
    {-1, 1, 1, 8}, { 0, 1, 1, 8}, { 1, 1, 1, 8},
    { 0, 2, 1, 8},
};

const Tap* pickKernel(DitherMethod m, int& n) {
  switch (m) {
    case DITHER_JARVIS:   n = sizeof(kJarvis)   / sizeof(Tap); return kJarvis;
    case DITHER_ATKINSON: n = sizeof(kAtkinson) / sizeof(Tap); return kAtkinson;
    default:              n = sizeof(kFS)       / sizeof(Tap); return kFS;
  }
}

inline int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// Unweighted squared distance, matching Seeed's reference implementation.
inline int nearest(int r, int g, int b) {
  int best = 0;
  int bestD = (r - kPal[0].r) * (r - kPal[0].r) + (g - kPal[0].g) * (g - kPal[0].g) +
              (b - kPal[0].b) * (b - kPal[0].b);
  for (int i = 1; i < 6; ++i) {
    const int dr = r - kPal[i].r, dg = g - kPal[i].g, db = b - kPal[i].b;
    const int d = dr * dr + dg * dg + db * db;
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
}

// Keep accumulated error bounded so it always fits int16 even on pathological
// input (a long run of saturated pixels).
inline int16_t clampErr(int v) { return (int16_t)(v < -4096 ? -4096 : (v > 4096 ? 4096 : v)); }

}  // namespace

void e6CodeToRgb(uint8_t code, uint8_t* r, uint8_t* g, uint8_t* b) {
  for (int i = 0; i < 6; ++i) {
    if (kCode[i] == code) { *r = kPal[i].r; *g = kPal[i].g; *b = kPal[i].b; return; }
  }
  *r = *g = *b = 255;
}

bool E6Ditherer::begin(int width, DitherMethod method, float gamma) {
  end();
  w_ = width;
  m_ = method;
  y_ = 0;

  gammaIdentity_ = (gamma > 0.999f && gamma < 1.001f);
  for (int i = 0; i < 256; ++i) {
    gamma_[i] = gammaIdentity_
                    ? (uint8_t)i
                    : (uint8_t)clamp8((int)(powf(i / 255.0f, 1.0f / gamma) * 255.0f + 0.5f));
  }

  if (m_ == DITHER_NONE || m_ == DITHER_BAYER8) return true;  // no error buffer needed

  // 3 rows is enough for every kernel we support (max dy = 2). Deliberately on
  // the internal heap, not PSRAM: this buffer is touched for every pixel and
  // internal RAM is several times faster. 1600 * 3 * 3 * 2 = 28.8 kB.
  const size_t n = (size_t)w_ * 3 * 3;
  err_ = (int16_t*)calloc(n, sizeof(int16_t));
  if (!err_) {
    // Degrade loudly rather than silently: ordered dithering still looks far
    // better than no dithering at all.
    m_ = DITHER_BAYER8;
    return false;
  }
  return true;
}

void E6Ditherer::end() {
  if (err_) { free(err_); err_ = nullptr; }
  w_ = 0;
  y_ = 0;
}

void E6Ditherer::reset() {
  y_ = 0;
  if (err_) memset(err_, 0, (size_t)w_ * 3 * 3 * sizeof(int16_t));
}

void E6Ditherer::row(const uint8_t* rgb888, uint8_t* outCodes) {
  const int W = w_;

  if (m_ == DITHER_NONE) {
    for (int x = 0; x < W; ++x) {
      const uint8_t* p = rgb888 + x * 3;
      outCodes[x] = kCode[nearest(gamma_[p[0]], gamma_[p[1]], gamma_[p[2]])];
    }
    ++y_;
    return;
  }

  if (m_ == DITHER_BAYER8) {
    const int spread = 64;
    const uint8_t* brow = kBayer8 + (y_ & 7) * 8;
    for (int x = 0; x < W; ++x) {
      const uint8_t* p = rgb888 + x * 3;
      const int mod = ((brow[x & 7] * spread) >> 6) - (spread >> 1);
      outCodes[x] = kCode[nearest(clamp8(gamma_[p[0]] + mod),
                                  clamp8(gamma_[p[1]] + mod),
                                  clamp8(gamma_[p[2]] + mod))];
    }
    ++y_;
    return;
  }

  int nTaps = 0;
  const Tap* K = pickKernel(m_, nTaps);

  int16_t* e0 = errRow(y_);       // this row
  int16_t* e1 = errRow(y_ + 1);   // next row
  int16_t* e2 = errRow(y_ + 2);   // row after next
  int16_t* erows[3] = {e0, e1, e2};

  for (int x = 0; x < W; ++x) {
    const uint8_t* p = rgb888 + x * 3;
    const int o = x * 3;

    const int r = clamp8(gamma_[p[0]] + e0[o + 0]);
    const int g = clamp8(gamma_[p[1]] + e0[o + 1]);
    const int b = clamp8(gamma_[p[2]] + e0[o + 2]);

    const int q = nearest(r, g, b);
    outCodes[x] = kCode[q];

    const int er = r - kPal[q].r;
    const int eg = g - kPal[q].g;
    const int eb = b - kPal[q].b;
    if ((er | eg | eb) == 0) continue;

    for (int k = 0; k < nTaps; ++k) {
      const int nx = x + K[k].dx;
      if (nx < 0 || nx >= W) continue;
      int16_t* dst = erows[K[k].dy] + nx * 3;
      const int num = K[k].num, den = K[k].den;
      dst[0] = clampErr(dst[0] + er * num / den);
      dst[1] = clampErr(dst[1] + eg * num / den);
      dst[2] = clampErr(dst[2] + eb * num / den);
    }
  }

  // The row we just consumed becomes row y+3, so clear it before rotating on.
  memset(e0, 0, (size_t)W * 3 * sizeof(int16_t));
  ++y_;
}
