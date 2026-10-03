// The pre-dither processing stages, checked against independent
// reimplementations of the reference code rather than against themselves.
//
// Three of these are worth the trouble:
//
//   * the saturation shortcut, because src/imgproc.cpp claims it is EXACTLY
//     the references' HSL round trip rather than an approximation of it. That
//     claim has to be demonstrated, not asserted;
//   * the sliding-window clarity, because it replaces two full-image buffers
//     with two 9-row rings and the window bookkeeping is the kind of thing that
//     is off by one at the bottom edge and nowhere else;
//   * dynamic-range compression, because its whole job is to land on the
//     palette's measured endpoints and nothing else in the suite would notice
//     if it landed somewhere near them instead.
#include "imgproc.h"
#include "colorspace.h"
#include "e6_dither.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf("  FAIL: %s\n", m); fails++; } } while (0)

static int cl8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static uint16_t pack565(int r, int g, int b) {
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static void unpack565(uint16_t v, int* r, int* g, int* b) {
  *r = (((v >> 11) & 0x1F) * 255 + 15) / 31;
  *g = (((v >> 5) & 0x3F) * 255 + 31) / 63;
  *b = ((v & 0x1F) * 255 + 15) / 31;
}

// Run one RGB row through the real pointwise pipeline.
static void runRow(const ProcSettings& ps, const E6Palette& pal, uint8_t* rgb, int n) {
  ImgProcPlan plan;
  ImgProcRect rect{0, 0, 0, 0};
  imgprocAnalyze(nullptr, 0, 0, rect, pal, ps, &plan);
  imgprocRow(plan, rgb, n);
}

// ---------------------------------------------------------------------------
//  1. Saturation: the literal HSL round trip from
//     applySaturation() in epaper-image-convert/src/processor.js.
// ---------------------------------------------------------------------------
static void hslReference(double k, int* rp, int* gp, int* bp) {
  const double r = *rp / 255.0, g = *gp / 255.0, b = *bp / 255.0;
  const double mx = std::max(r, std::max(g, b));
  const double mn = std::min(r, std::min(g, b));
  const double l = (mx + mn) / 2.0;
  if (mx == mn) return;                       // grayscale, the reference skips

  const double d = mx - mn;
  const double s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);

  double h;
  if (mx == r)      h = ((g - b) / d + (g < b ? 6 : 0)) / 6;
  else if (mx == g) h = ((b - r) / d + 2) / 6;
  else              h = ((r - g) / d + 4) / 6;

  const double ns = std::max(0.0, std::min(1.0, s * k));
  const double c  = (1 - std::fabs(2 * l - 1)) * ns;
  const double x  = c * (1 - std::fabs(std::fmod(h * 6, 2.0) - 1));
  const double m  = l - c / 2;

  double rp2, gp2, bp2;
  const int sector = (int)std::floor(h * 6);
  if (sector == 0)      { rp2 = c; gp2 = x; bp2 = 0; }
  else if (sector == 1) { rp2 = x; gp2 = c; bp2 = 0; }
  else if (sector == 2) { rp2 = 0; gp2 = c; bp2 = x; }
  else if (sector == 3) { rp2 = 0; gp2 = x; bp2 = c; }
  else if (sector == 4) { rp2 = x; gp2 = 0; bp2 = c; }
  else                  { rp2 = c; gp2 = 0; bp2 = x; }

  *rp = cl8((int)std::lround((rp2 + m) * 255));
  *gp = cl8((int)std::lround((gp2 + m) * 255));
  *bp = cl8((int)std::lround((bp2 + m) * 255));
}

static void testSaturation() {
  printf("-- saturation: shortcut vs the references' HSL round trip\n");
  const E6Palette& pal = paletteBuiltin(PAL_SPECTRA6);

  for (double k : {0.0, 0.5, 0.9, 1.1, 1.6, 2.0}) {
    int worst = 0, worstPx[3] = {0, 0, 0};
    long n = 0;
    srand(7);
    std::vector<uint8_t> row;
    std::vector<int> refRow;
    // A spread of colours including neutrals, near-neutrals and fully
    // saturated primaries, where the S' clamp to 1 actually bites.
    for (int i = 0; i < 4000; ++i) {
      const int r = rand() & 0xFF, g = rand() & 0xFF, b = rand() & 0xFF;
      row.push_back((uint8_t)r); row.push_back((uint8_t)g); row.push_back((uint8_t)b);
      refRow.push_back(r); refRow.push_back(g); refRow.push_back(b);
    }
    for (int v : {0, 1, 127, 128, 254, 255}) {
      row.push_back((uint8_t)v); row.push_back((uint8_t)v); row.push_back((uint8_t)v);
      refRow.push_back(v); refRow.push_back(v); refRow.push_back(v);
      row.push_back(255); row.push_back((uint8_t)v); row.push_back(0);
      refRow.push_back(255); refRow.push_back(v); refRow.push_back(0);
    }

    ProcSettings ps;
    ps.saturation = (float)k;
    runRow(ps, pal, row.data(), (int)row.size() / 3);

    for (size_t i = 0; i < refRow.size(); i += 3) {
      int r = refRow[i], g = refRow[i + 1], b = refRow[i + 2];
      hslReference(k, &r, &g, &b);
      const int d = std::max(std::abs(r - row[i]),
                             std::max(std::abs(g - row[i + 1]), std::abs(b - row[i + 2])));
      if (d > worst) { worst = d; worstPx[0] = refRow[i]; worstPx[1] = refRow[i+1]; worstPx[2] = refRow[i+2]; }
      n++;
    }
    printf("   k=%.1f  %ld pixels, worst channel delta = %d  (at %d,%d,%d)\n",
           k, n, worst, worstPx[0], worstPx[1], worstPx[2]);
    // One unit, from the two implementations rounding at different points
    // (ours stays in 0..255, the reference normalises to 0..1 and back).
    CHECK(worst <= 1, "the saturation shortcut must match the HSL round trip to within rounding");
  }
}

// ---------------------------------------------------------------------------
//  2. S-curve: applyScurveTonemap() in epaper-image-convert.
// ---------------------------------------------------------------------------
static void testScurve() {
  printf("\n-- S-curve tone mapping vs applyScurveTonemap()\n");
  const E6Palette& pal = paletteBuiltin(PAL_SPECTRA6);

  struct Case { float strength, shadow, highlight, mid; };
  const Case cases[] = {
      {0.9f, 0.0f, 1.5f, 0.5f},    // the `dynamic` preset
      {0.7f, 0.1f, 1.3f, 0.5f},    // `vivid`
      {1.0f, 0.25f, 0.75f, 0.46f}, // `restore`
      {0.92f, 0.08f, 0.55f, 0.44f} // `posterScan`
  };

  for (const auto& c : cases) {
    std::vector<uint8_t> row(256 * 3);
    for (int i = 0; i < 256; ++i) { row[i*3] = (uint8_t)i; row[i*3+1] = (uint8_t)i; row[i*3+2] = (uint8_t)i; }

    ProcSettings ps;
    ps.toneMode = TONE_SCURVE;
    ps.scStrength = c.strength; ps.scShadow = c.shadow;
    ps.scHighlight = c.highlight; ps.scMidpoint = c.mid;
    runRow(ps, pal, row.data(), 256);

    int worst = 0;
    for (int i = 0; i < 256; ++i) {
      const double n = i / 255.0;
      double r;
      if (n <= c.mid) r = std::pow(n / c.mid, 1.0 - c.strength * c.shadow) * c.mid;
      else            r = c.mid + std::pow((n - c.mid) / (1.0 - c.mid),
                                           1.0 + c.strength * c.highlight) * (1.0 - c.mid);
      const int want = cl8((int)std::lround(std::max(0.0, std::min(1.0, r)) * 255));
      worst = std::max(worst, std::abs(want - (int)row[i * 3]));
    }
    printf("   strength=%.2f shadow=%.2f highlight=%.2f mid=%.2f -> worst delta %d\n",
           c.strength, c.shadow, c.highlight, c.mid, worst);
    CHECK(worst <= 1, "the S-curve LUT must match the reference curve");
  }

  // The curve must stay monotonic, or a gradient would develop a reversal.
  {
    std::vector<uint8_t> row(256 * 3);
    for (int i = 0; i < 256; ++i) { row[i*3] = (uint8_t)i; row[i*3+1] = (uint8_t)i; row[i*3+2] = (uint8_t)i; }
    ProcSettings ps;
    ps.toneMode = TONE_SCURVE;
    ps.scStrength = 1.0f; ps.scShadow = 1.0f; ps.scHighlight = 5.0f; ps.scMidpoint = 0.3f;
    runRow(ps, pal, row.data(), 256);
    int inversions = 0;
    for (int i = 1; i < 256; ++i) if (row[i * 3] < row[(i - 1) * 3]) inversions++;
    printf("   extreme settings: %d inversions in the ramp\n", inversions);
    CHECK(inversions == 0, "the tone curve must be monotonic at every setting");
  }
}

// ---------------------------------------------------------------------------
//  3. Dynamic range compression: must land on the palette's own endpoints.
// ---------------------------------------------------------------------------
static void testDrc() {
  printf("\n-- dynamic range compression lands on the palette endpoints\n");
  csInit();

  struct { uint8_t id; const char* n; } pals[] = {
      {PAL_SPECTRA6, "spectra6"}, {PAL_AITJCIZE, "aitjcize"}, {PAL_SEEED, "seeed"}};

  for (auto& p : pals) {
    const E6Palette& pal = paletteBuiltin(p.id);
    const int ib = paletteDarkest(pal), iw = paletteLightest(pal);
    const float blackY = csLinearY(pal.rgb[ib][0], pal.rgb[ib][1], pal.rgb[ib][2]);
    const float whiteY = csLinearY(pal.rgb[iw][0], pal.rgb[iw][1], pal.rgb[iw][2]);

    for (int quality = 0; quality < 2; ++quality) {
      uint8_t row[6] = {255, 255, 255, 0, 0, 0};   // pure white, then pure black
      ProcSettings ps;
      ps.drcMode = DRC_DISPLAY;
      ps.drcStrength = 1.0f;
      ps.drcQuality = quality ? DRCQ_ACCURATE : DRCQ_FAST;
      runRow(ps, pal, row, 2);

      const float gotW = csLinearY(row[0], row[1], row[2]);
      const float gotB = csLinearY(row[3], row[4], row[5]);
      printf("   %-9s %-8s white Y %.4f (want %.4f)  black Y %.4f (want %.4f)\n",
             p.n, quality ? "accurate" : "fast", gotW, whiteY, gotB, blackY);
      // Tolerance is the 4096-bin linear->sRGB reverse table plus the 8-bit
      // output quantisation, not slack in the maths.
      CHECK(std::fabs(gotW - whiteY) < 0.004f, "white must compress to the palette's white luminance");
      CHECK(std::fabs(gotB - blackY) < 0.004f, "black must compress to the palette's black luminance");
    }
  }

  // Chroma protection: a fully saturated ink must be held back from the
  // flattening that a neutral of the same luminance receives.
  {
    const E6Palette& pal = paletteBuiltin(PAL_SPECTRA6);
    uint8_t sat[3] = {255, 0, 0};
    uint8_t neu[3] = {0, 0, 0};
    // Match the neutral's luminance to the saturated pixel's.
    const float satY = csLinearY(255, 0, 0);
    const uint8_t v = csLinearToSrgb(satY);
    neu[0] = neu[1] = neu[2] = v;
    const float before = csLinearY(sat[0], sat[1], sat[2]);

    ProcSettings ps;
    ps.drcMode = DRC_DISPLAY; ps.drcStrength = 1.0f;
    runRow(ps, pal, sat, 1);
    runRow(ps, pal, neu, 1);

    const float satAfter = csLinearY(sat[0], sat[1], sat[2]);
    const float neuAfter = csLinearY(neu[0], neu[1], neu[2]);
    const float satMoved = std::fabs(satAfter - before);
    const float neuMoved = std::fabs(neuAfter - satY);
    printf("   chroma protection: saturated moved %.4f, equal-luminance neutral moved %.4f\n",
           satMoved, neuMoved);
    CHECK(satMoved < neuMoved, "a saturated pixel must be compressed less than a neutral one");
  }

  // DRC_AUTO must read the source percentiles rather than assuming 0..1.
  {
    const E6Palette& pal = paletteBuiltin(PAL_SPECTRA6);
    const int W = 200, H = 200;
    std::vector<uint16_t> px((size_t)W * H);
    // Luminance confined to a band in the middle: p1/p99 should find its edges,
    // not 0 and 1.
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const int v = 100 + (x * 60) / W;      // 100..159, neutral
        px[(size_t)y * W + x] = pack565(v, v, v);
      }

    ProcSettings ps;
    ps.drcMode = DRC_AUTO; ps.drcStrength = 1.0f;
    ps.drcLowPct = 0.01f; ps.drcHighPct = 0.99f;

    ImgProcPlan plan;
    ImgProcRect rect{0, 0, W - 1, H - 1};
    imgprocAnalyze(px.data(), W, H, rect, pal, ps, &plan);

    const float loY = csLinearY(100, 100, 100), hiY = csLinearY(159, 159, 159);
    printf("   auto percentiles: srcLo=%.4f srcRange=%.4f (band is %.4f..%.4f)\n",
           plan.drcSrcLo, plan.drcSrcRange, loY, hiY);
    CHECK(plan.drcMode == DRC_AUTO, "auto mode must stay enabled for a non-flat image");
    CHECK(std::fabs(plan.drcSrcLo - loY) < 0.02f, "the low percentile must find the band floor");
    CHECK(std::fabs(plan.drcSrcLo + plan.drcSrcRange - hiY) < 0.02f,
          "the high percentile must find the band ceiling");

    // A flat image has no range to stretch; auto must switch itself off rather
    // than divide by zero.
    for (auto& v : px) v = pack565(128, 128, 128);
    imgprocAnalyze(px.data(), W, H, rect, pal, ps, &plan);
    printf("   flat image: drcMode=%s\n", drcModeName(plan.drcMode));
    CHECK(plan.drcMode == DRC_OFF, "auto mode must disable itself on a flat image");
  }

  // The visible rect must be honoured: tones outside it cannot set the range.
  {
    const E6Palette& pal = paletteBuiltin(PAL_SPECTRA6);
    const int W = 200, H = 200;
    std::vector<uint16_t> px((size_t)W * H);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x)
        px[(size_t)y * W + x] = pack565(x < W / 2 ? 20 : 200, x < W / 2 ? 20 : 200,
                                        x < W / 2 ? 20 : 200);

    ProcSettings ps;
    ps.drcMode = DRC_AUTO; ps.drcStrength = 1.0f;
    ImgProcPlan whole, half;
    imgprocAnalyze(px.data(), W, H, {0, 0, W - 1, H - 1}, pal, ps, &whole);
    imgprocAnalyze(px.data(), W, H, {W / 2, 0, W - 1, H - 1}, pal, ps, &half);
    printf("   rect honoured: whole-image srcLo=%.4f, right-half-only srcLo=%.4f\n",
           whole.drcSrcLo, half.drcSrcLo);
    CHECK(half.drcSrcLo > whole.drcSrcLo + 0.1f,
          "cropping away the dark half must raise the low percentile");
  }
}

// ---------------------------------------------------------------------------
//  4. Clarity: the sliding window must equal a whole-image box blur.
// ---------------------------------------------------------------------------
static void clarityReference(std::vector<uint16_t>& px, int w, int h, float amount, int radius,
                             float midtone) {
  const int kernel = radius * 2 + 1;
  const float eff = amount * 2.0f;

  // Whole-image, two-pass, the way applyClarity() does it.
  std::vector<int> src((size_t)w * h * 3), tmp((size_t)w * h * 3);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      int r, g, b;
      unpack565(px[(size_t)y * w + x], &r, &g, &b);
      const size_t o = ((size_t)y * w + x) * 3;
      src[o] = r; src[o + 1] = g; src[o + 2] = b;
    }

  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 3; ++c) {
        int sum = 0;
        for (int k = -radius; k <= radius; ++k) {
          const int xi = std::max(0, std::min(w - 1, x + k));
          sum += src[((size_t)y * w + xi) * 3 + c];
        }
        tmp[((size_t)y * w + x) * 3 + c] = sum / kernel;
      }

  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      int blur[3];
      for (int c = 0; c < 3; ++c) {
        int sum = 0;
        for (int k = -radius; k <= radius; ++k) {
          const int yi = std::max(0, std::min(h - 1, y + k));
          sum += tmp[((size_t)yi * w + x) * 3 + c];
        }
        blur[c] = sum;
      }
      const size_t o = ((size_t)y * w + x) * 3;
      const float l = csLuma709(src[o], src[o + 1], src[o + 2]) / 255.0f;
      const float wgt =
          std::pow(std::max(0.0f, std::min(1.0f, 1.0f - std::fabs(2 * l - 1))), midtone);
      int out[3];
      for (int c = 0; c < 3; ++c)
        out[c] = cl8((int)(src[o + c] + eff * (src[o + c] - blur[c] / (float)kernel) * wgt + 0.5f));
      px[(size_t)y * w + x] = pack565(out[0], out[1], out[2]);
    }
}

static void testClarity() {
  printf("\n-- clarity: sliding window vs a whole-image box blur\n");

  // The reference weights its midtone mask by the luma of the POST-gamma,
  // post-paper source, which here is just the source: gamma 1.0, paper off.
  for (int radius : {1, 2, 3, 4}) {
    const int W = 83, H = 47;                  // small and prime-ish, so edges dominate
    std::vector<uint16_t> a((size_t)W * H), b;
    srand(99);
    for (auto& v : a) v = pack565(rand() & 0xFF, rand() & 0xFF, rand() & 0xFF);
    b = a;

    ProcSettings ps;
    ps.clarityAmount = 0.35f;
    ps.clarityRadius = (uint8_t)radius;
    ps.clarityMidtone = 1.2f;
    // outScale 1.0 so the radius passes through unchanged and the reference can
    // use the same number.
    const bool ok = imgprocSourcePass(a.data(), W, H, ps, 1.0f, 1.0f);
    clarityReference(b, W, H, ps.clarityAmount, radius, ps.clarityMidtone);

    size_t diff = 0;
    int firstY = -1;
    for (size_t i = 0; i < a.size(); ++i)
      if (a[i] != b[i]) { if (firstY < 0) firstY = (int)(i / W); diff++; }
    printf("   radius=%d  alloc=%s  %zu/%zu pixels differ%s\n", radius, ok ? "ok" : "FAILED",
           diff, a.size(), diff ? "" : "  -> window == whole-image, exactly");
    if (diff) printf("     first divergence at row %d of %d\n", firstY, H);
    CHECK(diff == 0, "the sliding-window clarity must equal the whole-image reference");
  }

  // Sweep midtone too, including the values below 1 where the weight function
  // has an unbounded derivative at the ends of the luma range.
  for (float midtone : {0.5f, 1.0f, 2.0f, 4.0f}) {
    const int W = 61, H = 37;
    std::vector<uint16_t> a((size_t)W * H), b;
    srand(21);
    for (auto& v : a) v = pack565(rand() & 0xFF, rand() & 0xFF, rand() & 0xFF);
    b = a;
    ProcSettings ps;
    ps.clarityAmount = -0.6f;                  // negative: softening, not sharpening
    ps.clarityRadius = 2;
    ps.clarityMidtone = midtone;
    imgprocSourcePass(a.data(), W, H, ps, 1.0f, 1.0f);
    clarityReference(b, W, H, ps.clarityAmount, 2, midtone);
    size_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) diff++;
    printf("   midtone=%.1f amount=-0.6  %zu/%zu pixels differ\n", midtone, diff, a.size());
    CHECK(diff == 0, "the clarity midtone weighting must be exact at every setting");
  }

  // A radius wider than the image is the edge case the clamping exists for.
  {
    const int W = 3, H = 2;
    std::vector<uint16_t> a((size_t)W * H), b;
    for (int i = 0; i < W * H; ++i) a[i] = pack565(i * 40, 128, 255 - i * 40);
    b = a;
    ProcSettings ps;
    ps.clarityAmount = 0.5f; ps.clarityRadius = 4; ps.clarityMidtone = 1.0f;
    imgprocSourcePass(a.data(), W, H, ps, 1.0f, 1.0f);
    clarityReference(b, W, H, 0.5f, 4, 1.0f);
    const bool same = a == b;
    printf("   3x2 image with radius 4: %s\n", same ? "matches" : "DIFFERS");
    CHECK(same, "clamping must hold when the kernel is wider than the image");
  }

  // The radius is specified in OUTPUT pixels, so halving the render scale must
  // double the radius used on the source.
  {
    const int W = 60, H = 60;
    std::vector<uint16_t> a((size_t)W * H), b;
    srand(5);
    for (auto& v : a) v = pack565(rand() & 0xFF, rand() & 0xFF, rand() & 0xFF);
    b = a;
    ProcSettings ps;
    ps.clarityAmount = 0.4f; ps.clarityRadius = 2; ps.clarityMidtone = 1.2f;
    imgprocSourcePass(a.data(), W, H, ps, 1.0f, 0.5f);   // scale 0.5 -> radius 4
    clarityReference(b, W, H, 0.4f, 4, 1.2f);
    const bool same = a == b;
    printf("   outScale 0.5 with radius 2 behaves as radius 4: %s\n", same ? "yes" : "NO");
    CHECK(same, "the clarity radius must be expressed in output pixels");
  }
}

// ---------------------------------------------------------------------------
//  5. Gating, paper normalisation and presets.
// ---------------------------------------------------------------------------
static void testGatingAndPresets() {
  printf("\n-- neutral settings must be a true no-op\n");
  {
    const int W = 64, H = 64;
    std::vector<uint16_t> a((size_t)W * H), b;
    srand(3);
    for (auto& v : a) v = pack565(rand() & 0xFF, rand() & 0xFF, rand() & 0xFF);
    b = a;
    ProcSettings ps;                                  // all defaults
    CHECK(!imgprocSourcePassNeeded(ps, 1.0f), "neutral settings must skip the source pass");
    imgprocSourcePass(a.data(), W, H, ps, 1.0f, 1.0f);
    CHECK(a == b, "a skipped source pass must not touch a single pixel");

    std::vector<uint8_t> row(W * 3), ref;
    for (int i = 0; i < W * 3; ++i) row[i] = (uint8_t)(rand() & 0xFF);
    ref = row;
    ImgProcPlan plan;
    imgprocAnalyze(a.data(), W, H, {0, 0, W - 1, H - 1}, paletteBuiltin(PAL_SEEED), ps, &plan);
    printf("   plan.any = %s\n", plan.any ? "true" : "false");
    CHECK(!plan.any, "neutral settings must leave the row pass inactive");
    imgprocRow(plan, row.data(), W);
    CHECK(row == ref, "an inactive row pass must not touch a single pixel");
  }

  printf("\n-- gamma is still the first stage and still means what it meant\n");
  {
    const int W = 256, H = 1;
    std::vector<uint16_t> a((size_t)W);
    for (int i = 0; i < W; ++i) a[i] = pack565(i, i, i);
    ProcSettings ps;
    imgprocSourcePass(a.data(), W, H, ps, 2.0f, 1.0f);
    int worst = 0;
    for (int i = 0; i < W; ++i) {
      int r, g, b;
      unpack565(a[i], &r, &g, &b);
      int sr, sg, sb;
      unpack565(pack565(i, i, i), &sr, &sg, &sb);
      const int want = cl8((int)(std::pow(sr / 255.0, 1.0 / 2.0) * 255.0 + 0.5));
      int wr, wg, wb;
      unpack565(pack565(want, want, want), &wr, &wg, &wb);
      worst = std::max(worst, std::abs(wr - r));
    }
    printf("   gamma 2.0 on a 256-step ramp: worst delta %d\n", worst);
    CHECK(worst <= 1, "gamma must still be pow(v, 1/gamma)");
  }

  printf("\n-- paper normalisation neutralises warm paper and anchors dark ink\n");
  {
    const E6Palette& pal = paletteBuiltin(PAL_SPECTRA6);
    const int W = 3;
    // warm bright paper, dark neutral ink, saturated red ink
    std::vector<uint16_t> a = {pack565(240, 228, 205), pack565(60, 58, 56), pack565(200, 30, 24)};
    std::vector<uint16_t> before = a;
    ProcSettings ps;
    ps.paperMode = PAPER_WARM;
    ps.paperStrength = 0.95f;
    imgprocSourcePass(a.data(), W, 1, ps, 1.0f, 1.0f);
    (void)pal;

    int r0, g0, b0, r1, g1, b1, r2, g2, b2;
    unpack565(a[0], &r0, &g0, &b0);
    unpack565(a[1], &r1, &g1, &b1);
    unpack565(a[2], &r2, &g2, &b2);
    int pr, pg, pb;
    unpack565(before[1], &pr, &pg, &pb);
    printf("   paper 240,228,205 -> %d,%d,%d   ink 60,58,56 -> %d,%d,%d   red 200,30,24 -> %d,%d,%d\n",
           r0, g0, b0, r1, g1, b1, r2, g2, b2);
    CHECK(r0 - b0 < 240 - 205, "warm paper must lose its warm bias");
    CHECK(csLuma709(r1, g1, b1) < csLuma709(pr, pg, pb),
          "dark neutral ink must be anchored darker");
    CHECK(r2 >= 200, "saturated red ink must be preserved, not neutralised");
  }

  printf("\n-- presets carry the documented values\n");
  {
    struct Want { uint8_t id; const char* n; float sat; uint8_t tone; uint8_t drc;
                  uint8_t cm; uint8_t ed; };
    const Want want[] = {
        {PROC_CUSTOM,     "custom",     1.0f, TONE_OFF,      DRC_OFF,     CM_RGB, ED_FLOYD_STEINBERG},
        {PROC_BALANCED,   "balanced",   1.0f, TONE_CONTRAST, DRC_DISPLAY, CM_RGB, ED_FLOYD_STEINBERG},
        {PROC_DYNAMIC,    "dynamic",    1.3f, TONE_SCURVE,   DRC_OFF,     CM_RGB, ED_FLOYD_STEINBERG},
        {PROC_VIVID,      "vivid",      1.6f, TONE_SCURVE,   DRC_OFF,     CM_RGB, ED_FLOYD_STEINBERG},
        {PROC_SOFT,       "soft",       1.1f, TONE_CONTRAST, DRC_DISPLAY, CM_RGB, ED_STUCKI},
        {PROC_GRAYSCALE,  "grayscale",  0.0f, TONE_SCURVE,   DRC_DISPLAY, CM_LAB, ED_FLOYD_STEINBERG},
        {PROC_RESTORE,    "restore",    0.9f, TONE_SCURVE,   DRC_AUTO,    CM_LAB, ED_FLOYD_STEINBERG},
        {PROC_POSTERSCAN, "posterScan", 1.05f, TONE_SCURVE,  DRC_AUTO,    CM_RGB, ED_FLOYD_STEINBERG},
    };
    for (const auto& w : want) {
      ProcSettings ps;
      ps.preset = w.id;
      uint8_t cm = 0xFF, ed = 0xFF;
      imgprocPreset(w.id, &ps, &cm, &ed);
      printf("   %-11s sat=%.2f tone=%-8s drc=%-7s match=%-6s kernel=%s\n", w.n, ps.saturation,
             toneModeName(ps.toneMode), drcModeName(ps.drcMode), colorMatchName(cm),
             edMatrixName(ed));
      CHECK(std::fabs(ps.saturation - w.sat) < 1e-5f, "preset saturation");
      CHECK(ps.toneMode == w.tone, "preset tone mode");
      CHECK(ps.drcMode == w.drc, "preset DRC mode");
      CHECK(cm == w.cm, "preset colour matching");
      CHECK(ed == w.ed, "preset kernel");
      CHECK(ps.preset == w.id, "imgprocPreset must not clobber the preset id");
      // Only the two range-fitting presets ask for white preservation, which is
      // what epdoptimize's auto suggestions do.
      CHECK(ps.drcPreserveWhite == (w.drc == DRC_AUTO), "preset white preservation");
    }
  }

  printf("\n-- white preservation needs a background white to protect\n");
  {
    const E6Palette& pal = paletteBuiltin(PAL_SPECTRA6);
    const int W = 100, H = 100;
    std::vector<uint16_t> px((size_t)W * H);
    ProcSettings ps;
    ps.drcMode = DRC_AUTO; ps.drcPreserveWhite = true;

    // A dark image has no paper to protect: whitePreserveMinLuma rejects it.
    for (auto& v : px) v = pack565(40, 40, 40);
    ImgProcPlan dark;
    imgprocAnalyze(px.data(), W, H, {0, 0, W - 1, H - 1}, pal, ps, &dark);
    printf("   all-dark image: presWhite=%s\n", dark.presWhite ? "true" : "false");
    CHECK(!dark.presWhite, "a dark image must not get a white point invented for it");

    // A scan with bright neutral paper and dark text does.
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x)
        px[(size_t)y * W + x] = (y % 10 == 0) ? pack565(30, 30, 30) : pack565(245, 245, 243);
    ImgProcPlan scan;
    imgprocAnalyze(px.data(), W, H, {0, 0, W - 1, H - 1}, pal, ps, &scan);
    printf("   paper-and-text scan: presWhite=%s presLuma=%.1f\n",
           scan.presWhite ? "true" : "false", scan.presLuma);
    CHECK(scan.presWhite, "a scan with bright paper must get white preservation");
    CHECK(scan.presLuma > 200.0f, "the protected luma must be the paper, not the text");

    // And the paper must come back out at the palette's white, not below it.
    //
    // The row the renderer feeds comes from bilinear-sampling the RGB565
    // source, so the literal 245,245,243 written above is stored and read back
    // as 247,247,247 -- which is also what the analysis pass binned. Feeding
    // the pre-quantisation value here would test a pixel the renderer never
    // produces, and it would sit just below the percentile and not snap.
    int qr, qg, qb;
    unpack565(pack565(245, 245, 243), &qr, &qg, &qb);
    uint8_t row[3] = {(uint8_t)qr, (uint8_t)qg, (uint8_t)qb};
    imgprocRow(scan, row, 1);
    const int iw = paletteLightest(pal);
    printf("   paper pixel %d,%d,%d -> %d,%d,%d (palette white %d,%d,%d)\n", qr, qg, qb, row[0],
           row[1], row[2], pal.rgb[iw][0], pal.rgb[iw][1], pal.rgb[iw][2]);
    CHECK(row[0] == pal.rgb[iw][0] && row[1] == pal.rgb[iw][1] && row[2] == pal.rgb[iw][2],
          "a background-white pixel must be snapped to the palette white");
  }
}

int main() {
  csInit();
  testSaturation();
  testScurve();
  testDrc();
  testClarity();
  testGatingAndPresets();
  printf(fails ? "\n%d FAILED\n" : "\nall checks passed\n", fails);
  return fails ? 1 : 0;
}
