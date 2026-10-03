// Proves the streaming ditherer's 3-row rolling error buffer is equivalent to a
// plain whole-image implementation, for every kernel and in both scan
// directions. If the buffer rotation, the clearing of the consumed row, any dy
// offset, or the serpentine dx mirroring were wrong, these would diverge.
//
// The kernel table below is deliberately a SECOND copy of the one in
// src/e6_dither.cpp rather than a shared header. This test exists to check the
// buffer machinery, not the weights; sharing the table would make it blind to
// the thing it does test (a wrong dy sends error to the wrong ring slot), and
// the weights are checked separately in test_dither.cpp.
#include "e6_dither.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct Tap { int dx, dy, num, den; };

static const Tap FS[]   = {{1,0,7,16},{-1,1,3,16},{0,1,5,16},{1,1,1,16}};
static const Tap FFS[]  = {{1,0,3,8},{0,1,3,8},{1,1,2,8}};
static const Tap AT[]   = {{1,0,1,8},{2,0,1,8},{-1,1,1,8},{0,1,1,8},{1,1,1,8},{0,2,1,8}};
static const Tap JV[]   = {{1,0,7,48},{2,0,5,48},{-2,1,3,48},{-1,1,5,48},{0,1,7,48},{1,1,5,48},
                           {2,1,3,48},{-2,2,1,48},{-1,2,3,48},{0,2,5,48},{1,2,3,48},{2,2,1,48}};
static const Tap ST[]   = {{1,0,8,42},{2,0,4,42},{-2,1,2,42},{-1,1,4,42},{0,1,8,42},{1,1,4,42},
                           {2,1,2,42},{-2,2,1,42},{-1,2,2,42},{0,2,4,42},{1,2,2,42},{2,2,1,42}};
static const Tap BU[]   = {{1,0,8,32},{2,0,4,32},{-2,1,2,32},{-1,1,4,32},{0,1,8,32},{1,1,4,32},
                           {2,1,2,32}};
static const Tap S3[]   = {{1,0,5,32},{2,0,3,32},{-2,1,2,32},{-1,1,4,32},{0,1,5,32},{1,1,4,32},
                           {2,1,2,32},{-1,2,2,32},{0,2,3,32},{1,2,2,32}};
static const Tap S2[]   = {{1,0,4,16},{2,0,3,16},{-2,1,1,16},{-1,1,2,16},{0,1,3,16},{1,1,2,16},
                           {2,1,1,16}};
static const Tap S24[]  = {{1,0,2,4},{-1,1,1,4},{0,1,1,4}};
static const Tap FAN[]  = {{1,0,7,16},{-2,1,1,16},{-1,1,3,16},{0,1,5,16}};
static const Tap SF[]   = {{1,0,4,8},{-2,1,1,8},{-1,1,1,8},{0,1,2,8}};
static const Tap SF2[]  = {{1,0,7,14},{-3,1,1,14},{-2,1,1,14},{-1,1,2,14},{0,1,3,14}};

static int cl8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static int16_t cle(int v) { return (int16_t)(v < -4096 ? -4096 : (v > 4096 ? 4096 : v)); }

static const E6Palette& pal() {
  static const E6Palette p = paletteBuiltin(PAL_SEEED);
  return p;
}
static const uint8_t C[6] = {0x0, 0x2, 0x6, 0xB, 0xD, 0xF};

static int nearest(int r, int g, int b) {
  int best = 0, bd = 1 << 30;
  for (int i = 0; i < 6; i++) {
    const int dr = r - pal().rgb[i][0], dg = g - pal().rgb[i][1], db = b - pal().rgb[i][2];
    const int d = dr * dr + dg * dg + db * db;
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}

// Whole-image reference: one full-size signed error plane, no rolling at all.
static void reference(const uint8_t* rgb, int W, int H, const Tap* K, int nk, bool serp,
                      uint8_t* out) {
  std::vector<int16_t> err((size_t)W * H * 3, 0);
  for (int y = 0; y < H; y++) {
    const bool rev = serp && (y & 1);
    const int xs = rev ? W - 1 : 0, xe = rev ? -1 : W, dx = rev ? -1 : 1;
    for (int x = xs; x != xe; x += dx) {
      const size_t o = ((size_t)y * W + x) * 3;
      const int r = cl8(rgb[o + 0] + err[o + 0]);
      const int g = cl8(rgb[o + 1] + err[o + 1]);
      const int b = cl8(rgb[o + 2] + err[o + 2]);
      const int q = nearest(r, g, b);
      out[(size_t)y * W + x] = C[q];
      const int er = r - pal().rgb[q][0], eg = g - pal().rgb[q][1], eb = b - pal().rgb[q][2];
      if ((er | eg | eb) == 0) continue;
      for (int k = 0; k < nk; k++) {
        const int nx = x + (rev ? -K[k].dx : K[k].dx), ny = y + K[k].dy;
        if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
        const size_t no = ((size_t)ny * W + nx) * 3;
        err[no + 0] = cle(err[no + 0] + er * K[k].num / K[k].den);
        err[no + 1] = cle(err[no + 1] + eg * K[k].num / K[k].den);
        err[no + 2] = cle(err[no + 2] + eb * K[k].num / K[k].den);
      }
    }
  }
}

int main() {
  const int W = 211, H = 157;                   // deliberately not multiples of 3
  std::vector<uint8_t> img((size_t)W * H * 3);
  srand(12345);
  for (size_t i = 0; i < img.size(); i++) img[i] = rand() & 0xFF;

  struct Case { EdMatrix m; const char* n; const Tap* k; int nk; };
  const Case cases[] = {
      {ED_FLOYD_STEINBERG, "floydSteinberg", FS, 4},
      {ED_FALSE_FLOYD_STEINBERG, "falseFS", FFS, 3},
      {ED_ATKINSON, "atkinson", AT, 6},
      {ED_JARVIS, "jarvis", JV, 12},
      {ED_STUCKI, "stucki", ST, 12},
      {ED_BURKES, "burkes", BU, 7},
      {ED_SIERRA3, "sierra3", S3, 10},
      {ED_SIERRA2, "sierra2", S2, 7},
      {ED_SIERRA2_4A, "sierra2-4a", S24, 3},
      {ED_FAN, "fan", FAN, 4},
      {ED_SHIAU_FAN, "shiauFan", SF, 4},
      {ED_SHIAU_FAN2, "shiauFan2", SF2, 5},
  };

  int fails = 0;
  for (int serp = 0; serp < 2; ++serp) {
    printf("-- serpentine=%d\n", serp);
    for (const auto& c : cases) {
      std::vector<uint8_t> ref((size_t)W * H), got((size_t)W * H);
      reference(img.data(), W, H, c.k, c.nk, serp != 0, ref.data());

      DitherCfg cfg;
      cfg.type = DITHER_ERROR_DIFFUSION;
      cfg.matrix = c.m;
      cfg.matching = CM_RGB;
      cfg.serpentine = serp != 0;

      E6Ditherer d;
      d.begin(W, pal(), cfg);
      for (int y = 0; y < H; y++)
        d.row(img.data() + (size_t)y * W * 3, got.data() + (size_t)y * W);
      d.end();

      size_t diff = 0; int firstY = -1;
      for (size_t i = 0; i < ref.size(); i++)
        if (ref[i] != got[i]) { if (firstY < 0) firstY = (int)(i / W); diff++; }
      printf("   %-16s %zu/%zu pixels differ%s\n", c.n, diff, ref.size(),
             diff ? "" : "  -> streaming == whole-image, exactly");
      if (diff) { printf("     FAIL: first divergence at row %d\n", firstY); fails++; }
    }
  }

  // reset() must leave the ditherer exactly as begin() did, or re-rendering a
  // second image without reallocating would inherit the first one's error.
  {
    DitherCfg cfg;
    std::vector<uint8_t> a((size_t)W * H), b((size_t)W * H);
    E6Ditherer d;
    d.begin(W, pal(), cfg);
    for (int y = 0; y < H; y++) d.row(img.data() + (size_t)y * W * 3, a.data() + (size_t)y * W);
    d.reset();
    for (int y = 0; y < H; y++) d.row(img.data() + (size_t)y * W * 3, b.data() + (size_t)y * W);
    d.end();
    const bool same = memcmp(a.data(), b.data(), a.size()) == 0;
    printf("-- reset(): second pass %s first\n", same ? "matches" : "DIFFERS from");
    if (!same) { printf("   FAIL: reset() left stale error behind\n"); fails++; }
  }

  printf(fails ? "\n%d FAILED\n" : "\nall checks passed\n", fails);
  return fails ? 1 : 0;
}
