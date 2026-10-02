#include "decode_internal.h"
#include "log.h"

#include <PNGdec.h>

namespace {

PNG       g_png;
SrcImage* g_out = nullptr;
uint16_t* g_line = nullptr;   // one scratch row for the RGB565 conversion

int32_t onRead(PNGFILE* f, uint8_t* buf, int32_t len) { (void)f; return decFileRead(buf, len); }
int32_t onSeek(PNGFILE* f, int32_t pos)               { (void)f; return decFileSeek(pos); }

// NOTE: PNGdec's draw callback returns int (unlike some older versions of the
// library, where it returned void). Returning 0 would abort the decode.
int onDraw(PNGDRAW* pDraw) {
  if (!g_out || !g_out->px || !g_line) return 0;
  const int W = g_out->w, H = g_out->h;
  if (pDraw->y < 0 || pDraw->y >= H) return 1;
  // Alpha is composited over white: a transparent PNG on an e-paper frame should
  // read as paper, not as black.
  g_png.getLineAsRGB565(pDraw, g_line, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  int n = pDraw->iWidth;
  if (n > W) n = W;
  memcpy(g_out->px + (size_t)pDraw->y * W, g_line, (size_t)n * sizeof(uint16_t));
  return 1;
}

}  // namespace

DecodeResult decodePngFile(const char* path, size_t budget, SrcImage* out) {
  if (g_png.open(path, decFileOpen, decFileClose, onRead, onSeek, onDraw) != PNG_SUCCESS) {
    LOG.println("[dec] PNG open failed");
    return DEC_CORRUPT;
  }
  const int w = g_png.getWidth();
  const int h = g_png.getHeight();
  LOG.printf("[dec] PNG %dx%d (%u kB)\n", w, h, (unsigned)((size_t)w * h * 2 / 1024));

  // PNG has no decode-time downscale, so an oversized PNG is a hard no. A clear
  // message beats a watchdog reboot.
  if ((size_t)w * h * 2 > budget) {
    g_png.close();
    return DEC_TOO_BIG;
  }
  if (!srcAlloc(out, w, h, budget)) { g_png.close(); return DEC_OOM; }

  g_line = (uint16_t*)malloc((size_t)w * sizeof(uint16_t));
  if (!g_line) { g_png.close(); srcFree(out); return DEC_OOM; }

  g_out = out;
  const int rc = g_png.decode(nullptr, 0);
  g_png.close();
  g_out = nullptr;
  free(g_line);
  g_line = nullptr;

  if (rc != PNG_SUCCESS) {
    LOG.println("[dec] PNG decode failed");
    srcFree(out);
    return DEC_CORRUPT;
  }
  return DEC_OK;
}
