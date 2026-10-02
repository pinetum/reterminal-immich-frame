// Proves the lib/JPEGDEC patches do what they claim: that an SOF1 frame with
// three AC Huffman tables -- what Immich's mozjpeg-backed sharp emits for every
// `preview` rendition -- decodes, and decodes to the same pixels as the plain
// baseline arrangement of the identical scan data.
//
// Without the patches the second open() returns JPEG_UNSUPPORTED_FEATURE (3),
// which is exactly the failure the device hit on every photo.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "JPEGDEC.h"
#include "jpeg_samples.h"

namespace {

std::vector<uint16_t> g_px;
int g_w, g_h;

int onDraw(JPEGDRAW* d) {
  for (int row = 0; row < d->iHeight; ++row) {
    const int dy = d->y + row;
    if (dy < 0 || dy >= g_h) continue;
    int n = d->iWidth;
    if (d->x + n > g_w) n = g_w - d->x;
    if (n <= 0) continue;
    memcpy(&g_px[(size_t)dy * g_w + d->x], d->pPixels + (size_t)row * d->iWidth,
           (size_t)n * sizeof(uint16_t));
  }
  return 1;
}

int fail = 0;

void check(bool ok, const char* what) {
  printf("%-52s %s\n", what, ok ? "ok" : "FAILED");
  if (!ok) fail = 1;
}

// Decode one in-memory JPEG to RGB565. Returns false with the error code in
// *err if JPEGDEC rejects it.
bool decode(const unsigned char* data, int len, std::vector<uint16_t>* out, int* err) {
  JPEGDEC jpg;
  if (!jpg.openRAM((uint8_t*)data, len, onDraw)) { *err = jpg.getLastError(); return false; }
  g_w = jpg.getWidth();
  g_h = jpg.getHeight();
  g_px.assign((size_t)g_w * g_h, 0);
  jpg.setPixelType(RGB565_LITTLE_ENDIAN);
  const int rc = jpg.decode(0, 0, 0);
  *err = jpg.getLastError();
  jpg.close();
  if (rc != 1) return false;
  *out = g_px;
  return true;
}

}  // namespace

int main() {
  std::vector<uint16_t> base, sof1;
  int errA = 0, errB = 0;

  const bool okA = decode(kBaselineJpeg, (int)sizeof(kBaselineJpeg), &base, &errA);
  printf("baseline SOF0, 2 AC tables : %s (err %d) %dx%d\n",
         okA ? "decoded" : "REJECTED", errA, g_w, g_h);
  check(okA, "baseline JPEG decodes");

  const bool okB = decode(kSof1Jpeg, (int)sizeof(kSof1Jpeg), &sof1, &errB);
  printf("Immich-style SOF1, 3 AC tables : %s (err %d) %dx%d\n",
         okB ? "decoded" : "REJECTED", errB, g_w, g_h);
  check(okB, "SOF1 with a third AC Huffman table decodes");

  if (okA && okB) {
    check(base.size() == sof1.size(), "both decodes are the same size");
    size_t diff = 0;
    for (size_t i = 0; i < base.size() && i < sof1.size(); ++i)
      if (base[i] != sof1[i]) ++diff;
    printf("differing pixels: %zu of %zu\n", diff, base.size());
    check(diff == 0, "the two arrangements decode to identical pixels");

    // A sanity floor: a 64x48 swirl is not a flat colour, so a decoder that
    // silently produced an empty image would still pass the compare above.
    size_t distinct = 0;
    for (size_t i = 1; i < base.size(); ++i)
      if (base[i] != base[i - 1]) ++distinct;
    printf("colour transitions: %zu\n", distinct);
    check(distinct > 100, "the decoded image is not blank");
  }

  printf(fail ? "\nJPEG checks FAILED\n" : "\nall checks passed\n");
  return fail;
}
