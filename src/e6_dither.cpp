#include "e6_dither.h"
#include "colorspace.h"
#include <math.h>

namespace {

const uint8_t kCode[PAL_SLOTS] = {E6_WHITE, E6_GREEN, E6_RED, E6_YELLOW, E6_BLUE, E6_BLACK};

struct Tap { int8_t dx, dy; int16_t num, den; };

// Every kernel from epdoptimize/src/dither/data/diffusion-maps.ts, flattened
// into one table so the enum indexes a {offset, count} pair rather than a
// pointer. Max dy is 2 and max |dx| is 3 (shiauFan2), which is what lets the
// error ring stay at three rows.
const Tap kTaps[] = {
    // ED_FLOYD_STEINBERG
    {1, 0, 7, 16}, {-1, 1, 3, 16}, {0, 1, 5, 16}, {1, 1, 1, 16},
    // ED_FALSE_FLOYD_STEINBERG
    {1, 0, 3, 8}, {0, 1, 3, 8}, {1, 1, 2, 8},
    // ED_ATKINSON
    {1, 0, 1, 8}, {2, 0, 1, 8}, {-1, 1, 1, 8}, {0, 1, 1, 8}, {1, 1, 1, 8}, {0, 2, 1, 8},
    // ED_JARVIS (Jarvis-Judice-Ninke). epdoptimize carries two spellings of
    // this kernel whose {0,2} weight differs (4/48 under `jarvis`, 5/48 under
    // `jarvisJudiceNinke`); 5/48 is the published JJN and the one this
    // firmware has always used, so there is only one entry here.
    {1, 0, 7, 48}, {2, 0, 5, 48},
    {-2, 1, 3, 48}, {-1, 1, 5, 48}, {0, 1, 7, 48}, {1, 1, 5, 48}, {2, 1, 3, 48},
    {-2, 2, 1, 48}, {-1, 2, 3, 48}, {0, 2, 5, 48}, {1, 2, 3, 48}, {2, 2, 1, 48},
    // ED_STUCKI
    {1, 0, 8, 42}, {2, 0, 4, 42},
    {-2, 1, 2, 42}, {-1, 1, 4, 42}, {0, 1, 8, 42}, {1, 1, 4, 42}, {2, 1, 2, 42},
    {-2, 2, 1, 42}, {-1, 2, 2, 42}, {0, 2, 4, 42}, {1, 2, 2, 42}, {2, 2, 1, 42},
    // ED_BURKES
    {1, 0, 8, 32}, {2, 0, 4, 32},
    {-2, 1, 2, 32}, {-1, 1, 4, 32}, {0, 1, 8, 32}, {1, 1, 4, 32}, {2, 1, 2, 32},
    // ED_SIERRA3
    {1, 0, 5, 32}, {2, 0, 3, 32},
    {-2, 1, 2, 32}, {-1, 1, 4, 32}, {0, 1, 5, 32}, {1, 1, 4, 32}, {2, 1, 2, 32},
    {-1, 2, 2, 32}, {0, 2, 3, 32}, {1, 2, 2, 32},
    // ED_SIERRA2
    {1, 0, 4, 16}, {2, 0, 3, 16},
    {-2, 1, 1, 16}, {-1, 1, 2, 16}, {0, 1, 3, 16}, {1, 1, 2, 16}, {2, 1, 1, 16},
    // ED_SIERRA2_4A
    {1, 0, 2, 4}, {-1, 1, 1, 4}, {0, 1, 1, 4},
    // ED_FAN
    {1, 0, 7, 16}, {-2, 1, 1, 16}, {-1, 1, 3, 16}, {0, 1, 5, 16},
    // ED_SHIAU_FAN
    {1, 0, 4, 8}, {-2, 1, 1, 8}, {-1, 1, 1, 8}, {0, 1, 2, 8},
    // ED_SHIAU_FAN2
    {1, 0, 7, 14}, {-3, 1, 1, 14}, {-2, 1, 1, 14}, {-1, 1, 2, 14}, {0, 1, 3, 14},
};

struct Kernel { uint8_t first, count; };
const Kernel kKernels[ED_MATRICES] = {
    {0, 4},   // ED_FLOYD_STEINBERG
    {4, 3},   // ED_FALSE_FLOYD_STEINBERG
    {7, 6},   // ED_ATKINSON
    {13, 12}, // ED_JARVIS
    {25, 12}, // ED_STUCKI
    {37, 7},  // ED_BURKES
    {44, 10}, // ED_SIERRA3
    {54, 7},  // ED_SIERRA2
    {61, 3},  // ED_SIERRA2_4A
    {64, 4},  // ED_FAN
    {68, 4},  // ED_SHIAU_FAN
    {72, 5},  // ED_SHIAU_FAN2
};

// Recursive Bayer construction from
// epdoptimize/src/dither/functions/bayer-matrix.ts. `size` must be a power of
// two; `out` is a size*size row-major matrix holding 0..size*size-1.
//
// This generates the canonical orientation, which is the TRANSPOSE of the
// hand-written 8x8 table this file used to carry. A transposed Bayer matrix is
// still a Bayer matrix -- the dot pattern is mirrored about the diagonal and
// the result is indistinguishable -- but it does mean Bayer output is not
// bit-identical with earlier firmware.
void buildBayer(int size, uint8_t* out) {
  out[0] = 0; out[1] = 2;
  out[2] = 3; out[3] = 1;
  for (int n = 2; n < size; n *= 2) {
    const int m = n * 2;
    // Walk the sub-matrix backwards so the in-place expansion never reads a
    // cell it has already overwritten.
    for (int y = n - 1; y >= 0; --y) {
      for (int x = n - 1; x >= 0; --x) {
        const int v = out[y * n + x] * 4;
        out[y * m + x]         = (uint8_t)v;
        out[y * m + n + x]     = (uint8_t)(v + 2);
        out[(n + y) * m + x]   = (uint8_t)(v + 3);
        out[(n + y) * m + n + x] = (uint8_t)(v + 1);
      }
    }
  }
}

uint8_t normalizeBayerSize(uint8_t v) {
  if (v <= 2) return 2;
  if (v <= 4) return 4;
  if (v <= 8) return 8;
  return 16;
}

// Deterministic per-pixel noise for DITHER_RANDOM. Deliberately a hash of the
// coordinates rather than a PRNG: the SD frame cache assumes a given photo and
// a given configuration always render to the same frame, and a stateful
// generator would make a re-render differ from the cached copy.
inline uint8_t hashNoise(int x, int y) {
  uint32_t h = (uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u;
  h ^= h >> 15; h *= 0x2C1B3C6Du;
  h ^= h >> 12; h *= 0x297A2D39u;
  h ^= h >> 15;
  return (uint8_t)(h & 0xFF);
}

// Keep accumulated error bounded so it always fits int16 even on pathological
// input (a long run of saturated pixels).
inline int16_t clampErr(int v) { return (int16_t)(v < -4096 ? -4096 : (v > 4096 ? 4096 : v)); }

}  // namespace

void e6CodeToRgb(const E6Palette& pal, uint8_t code, uint8_t* r, uint8_t* g, uint8_t* b) {
  for (int i = 0; i < PAL_SLOTS; ++i) {
    if (kCode[i] == code) {
      *r = pal.rgb[i][0]; *g = pal.rgb[i][1]; *b = pal.rgb[i][2];
      return;
    }
  }
  *r = pal.rgb[PAL_WHITE][0]; *g = pal.rgb[PAL_WHITE][1]; *b = pal.rgb[PAL_WHITE][2];
}

uint8_t e6SlotCode(int slot) {
  return (slot >= 0 && slot < PAL_SLOTS) ? kCode[slot] : E6_WHITE;
}

float e6KernelWeightSum(uint8_t matrix) {
  if (matrix >= ED_MATRICES) return 0.0f;
  const Kernel k = kKernels[matrix];
  float sum = 0.0f;
  for (int i = 0; i < k.count; ++i)
    sum += (float)kTaps[k.first + i].num / (float)kTaps[k.first + i].den;
  return sum;
}

const char* ditherTypeName(uint8_t t) {
  switch (t) {
    case DITHER_ERROR_DIFFUSION: return "errorDiffusion";
    case DITHER_ORDERED:         return "ordered";
    case DITHER_RANDOM:          return "random";
    case DITHER_QUANTIZE_ONLY:   return "quantizeOnly";
  }
  return "errorDiffusion";
}

const char* edMatrixName(uint8_t m) {
  switch (m) {
    case ED_FLOYD_STEINBERG:       return "floydSteinberg";
    case ED_FALSE_FLOYD_STEINBERG: return "falseFloydSteinberg";
    case ED_ATKINSON:              return "atkinson";
    case ED_JARVIS:                return "jarvis";
    case ED_STUCKI:                return "stucki";
    case ED_BURKES:                return "burkes";
    case ED_SIERRA3:               return "sierra3";
    case ED_SIERRA2:               return "sierra2";
    case ED_SIERRA2_4A:            return "sierra2-4a";
    case ED_FAN:                   return "fan";
    case ED_SHIAU_FAN:             return "shiauFan";
    case ED_SHIAU_FAN2:            return "shiauFan2";
  }
  return "floydSteinberg";
}

const char* colorMatchName(uint8_t m) {
  switch (m) {
    case CM_RGB:    return "rgb";
    case CM_LAB:    return "lab";
    case CM_CHROMA: return "chroma";
  }
  return "rgb";
}

bool E6Ditherer::begin(int width, const E6Palette& pal, const DitherCfg& cfg) {
  end();
  csInit();
  w_   = width;
  cfg_ = cfg;
  pal_ = pal;
  y_   = 0;

  if (cfg_.type >= DITHER_TYPES)   cfg_.type   = DITHER_ERROR_DIFFUSION;
  if (cfg_.matrix >= ED_MATRICES)  cfg_.matrix = ED_FLOYD_STEINBERG;
  if (cfg_.matching > CM_CHROMA)   cfg_.matching = CM_RGB;

  for (int i = 0; i < PAL_SLOTS; ++i) {
    const int r = pal_.rgb[i][0], g = pal_.rgb[i][1], b = pal_.rgb[i][2];
    csRgbToLab(r, g, b, palLab_[i]);
    palSat_[i] = csSaturation(r, g, b);
    palHue_[i] = csHue(r, g, b);
  }

  const uint8_t n = normalizeBayerSize(cfg_.bayerSize);
  cfg_.bayerSize = n;
  bayerMask_ = (uint8_t)(n - 1);
  {
    // Build the index matrix in place, then rescale to 0..255 so the threshold
    // maths below is independent of the matrix size.
    uint8_t idx[256];
    buildBayer(n, idx);
    const int cells = n * n;
    for (int i = 0; i < cells; ++i)
      bayer_[i] = (uint8_t)((idx[i] * 255) / (cells - 1));
  }

  if (cfg_.type != DITHER_ERROR_DIFFUSION) return true;  // no error buffer needed

  // 3 rows is enough for every kernel we support (max dy = 2). Deliberately on
  // the internal heap, not PSRAM: this buffer is touched for every pixel and
  // internal RAM is several times faster. 1600 * 3 * 3 * 2 = 28.8 kB.
  const size_t n_err = (size_t)w_ * 3 * 3;
  err_ = (int16_t*)calloc(n_err, sizeof(int16_t));
  if (!err_) {
    // Degrade loudly rather than silently: ordered dithering still looks far
    // better than no dithering at all.
    cfg_.type = DITHER_ORDERED;
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

int E6Ditherer::nearest(int r, int g, int b, int sr, int sg, int sb) const {
  switch (cfg_.matching) {
    case CM_LAB: {
      // findClosestColorLAB() in epaper-image-convert / the "lab" branch of
      // findClosestPaletteColor() in epdoptimize. dE is compared squared.
      float lab[3];
      csRgbToLab(r, g, b, lab);
      int   best  = 0;
      float bestD = csDeltaE2(lab, palLab_[0]);
      for (int i = 1; i < PAL_SLOTS; ++i) {
        const float d = csDeltaE2(lab, palLab_[i]);
        if (d < bestD) { bestD = d; best = i; }
      }
      return best;
    }

    case CM_CHROMA: {
      // epdoptimize's experimental "chroma" mode: stop saturated pastels from
      // collapsing into white by penalising neutral candidates in proportion
      // to how saturated the SOURCE pixel is, and penalising saturated
      // candidates by how far their hue is from the source's.
      //
      // The penalties are added to the unsquared distance, so unlike the other
      // two branches this one really does need the square root.
      const float srcSat = csSaturation(sr, sg, sb);
      const bool  useHue = srcSat >= 0.12f;
      const float srcHue = useHue ? csHue(sr, sg, sb) : 0.0f;

      int   best  = -1;
      float bestD = 0.0f;
      for (int i = 0; i < PAL_SLOTS; ++i) {
        const float dr = (float)(r - pal_.rgb[i][0]);
        const float dg = (float)(g - pal_.rgb[i][1]);
        const float db = (float)(b - pal_.rgb[i][2]);
        float d = sqrtf(dr * dr + dg * dg + db * db);

        if (srcSat >= 0.12f && palSat_[i] <= 0.12f)
          d += fminf(330.0f, srcSat * 1300.0f);
        if (useHue && palSat_[i] > 0.12f)
          d += csHueDistance(srcHue, palHue_[i]) * 3.0f;

        if (best < 0 || d < bestD) { bestD = d; best = i; }
      }
      return best;
    }

    case CM_RGB:
    default: {
      // Unweighted squared distance, as in find_closest_color() in
      // esp32-photoframe and distanceInColorSpace() in epdoptimize.
      int best = 0;
      int bestD = (r - pal_.rgb[0][0]) * (r - pal_.rgb[0][0]) +
                  (g - pal_.rgb[0][1]) * (g - pal_.rgb[0][1]) +
                  (b - pal_.rgb[0][2]) * (b - pal_.rgb[0][2]);
      for (int i = 1; i < PAL_SLOTS; ++i) {
        const int dr = r - pal_.rgb[i][0], dg = g - pal_.rgb[i][1], db = b - pal_.rgb[i][2];
        const int d = dr * dr + dg * dg + db * db;
        if (d < bestD) { bestD = d; best = i; }
      }
      return best;
    }
  }
}

void E6Ditherer::row(const uint8_t* rgb888, uint8_t* outCodes) {
  const int W = w_;

  if (cfg_.type == DITHER_QUANTIZE_ONLY) {
    for (int x = 0; x < W; ++x) {
      const uint8_t* p = rgb888 + x * 3;
      outCodes[x] = kCode[nearest(p[0], p[1], p[2], p[0], p[1], p[2])];
    }
    ++y_;
    return;
  }

  if (cfg_.type == DITHER_ORDERED || cfg_.type == DITHER_RANDOM) {
    const int spread = cfg_.orderedStrength;
    const int half   = spread >> 1;
    const uint8_t* brow = bayer_ + (size_t)(y_ & bayerMask_) * cfg_.bayerSize;
    for (int x = 0; x < W; ++x) {
      const uint8_t* p = rgb888 + x * 3;
      const int t = (cfg_.type == DITHER_ORDERED) ? brow[x & bayerMask_] : hashNoise(x, y_);
      const int mod = ((t * spread) >> 8) - half;
      outCodes[x] = kCode[nearest(csClamp8(p[0] + mod), csClamp8(p[1] + mod),
                                  csClamp8(p[2] + mod), p[0], p[1], p[2])];
    }
    ++y_;
    return;
  }

  // ---- error diffusion ------------------------------------------------------
  const Kernel K = kKernels[cfg_.matrix];
  const Tap* taps = kTaps + K.first;
  const int nTaps = K.count;

  int16_t* e0 = errRow(y_);       // this row
  int16_t* e1 = errRow(y_ + 1);   // next row
  int16_t* e2 = errRow(y_ + 2);   // row after next
  int16_t* erows[3] = {e0, e1, e2};

  // Serpentine: alternate the scan direction and mirror dx, so the error does
  // not all drift to the right and leave a diagonal grain. applyErrorDiffusion()
  // in epdoptimize/src/dither/dither.ts does exactly this.
  const bool rev = cfg_.serpentine && (y_ & 1);
  const int xStart = rev ? W - 1 : 0;
  const int xEnd   = rev ? -1 : W;
  const int xStep  = rev ? -1 : 1;

  for (int x = xStart; x != xEnd; x += xStep) {
    const uint8_t* p = rgb888 + x * 3;
    const int o = x * 3;

    const int r = csClamp8(p[0] + e0[o + 0]);
    const int g = csClamp8(p[1] + e0[o + 1]);
    const int b = csClamp8(p[2] + e0[o + 2]);

    const int q = nearest(r, g, b, p[0], p[1], p[2]);
    outCodes[x] = kCode[q];

    const int er = r - pal_.rgb[q][0];
    const int eg = g - pal_.rgb[q][1];
    const int eb = b - pal_.rgb[q][2];
    if ((er | eg | eb) == 0) continue;

    for (int k = 0; k < nTaps; ++k) {
      const int nx = x + (rev ? -taps[k].dx : taps[k].dx);
      if (nx < 0 || nx >= W) continue;
      int16_t* dst = erows[taps[k].dy] + nx * 3;
      const int num = taps[k].num, den = taps[k].den;
      dst[0] = clampErr(dst[0] + er * num / den);
      dst[1] = clampErr(dst[1] + eg * num / den);
      dst[2] = clampErr(dst[2] + eb * num / den);
    }
  }

  // The row we just consumed becomes row y+3, so clear it before rotating on.
  memset(e0, 0, (size_t)W * 3 * sizeof(int16_t));
  ++y_;
}
