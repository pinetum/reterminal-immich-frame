#include "render.h"
#include "settings.h"
#include "log.h"
#include "e6_dither.h"

namespace {

// RGB565 -> RGB888 expansion tables. Built once; saves a multiply-and-shift per
// channel per sample, and there are up to ~7.7 M samples per frame.
uint8_t g_lut5[32];
uint8_t g_lut6[64];
bool    g_lutReady = false;

void buildLuts() {
  if (g_lutReady) return;
  for (int i = 0; i < 32; ++i) g_lut5[i] = (uint8_t)((i * 255 + 15) / 31);
  for (int i = 0; i < 64; ++i) g_lut6[i] = (uint8_t)((i * 255 + 31) / 63);
  g_lutReady = true;
}

inline void unpack565(uint16_t v, int* r, int* g, int* b) {
  *r = g_lut5[(v >> 11) & 0x1F];
  *g = g_lut6[(v >> 5) & 0x3F];
  *b = g_lut5[v & 0x1F];
}

}  // namespace

uint8_t* frameAlloc() {
  uint8_t* f = (uint8_t*)ps_malloc(PANEL_FRAME_BYTES);
  if (!f) {
    LOG.printf("[render] frame alloc of %u kB FAILED\n",
               (unsigned)(PANEL_FRAME_BYTES / 1024));
    return nullptr;
  }
  memset(f, 0x00, PANEL_FRAME_BYTES);
  return f;
}

void frameFree(uint8_t* frame) { if (frame) free(frame); }

void frameClear(uint8_t* frame) {
  if (frame) memset(frame, 0x00, PANEL_FRAME_BYTES);   // 0x0 nibble == white
}

bool renderFrame(const SrcImage& src, uint8_t* frame, RenderStats* st) {
  if (!src.px || src.w < 1 || src.h < 1 || !frame) return false;
  buildLuts();
  const uint32_t t0 = millis();

  // ---- 1. decide rotation ---------------------------------------------------
  // The panel framebuffer is always panel-native portrait 1200x1600. "Rotating"
  // means composing onto a logical canvas and mapping its coordinates onto the
  // panel; the dither error still diffuses in logical scan order, which is the
  // order the eye reads the picture in. That is the correct place to rotate.
  int rot = g_cfg.rotation;
  // AUTO assumes the frame is turned CLOCKWISE for landscape viewing, which maps
  // the viewer's top-left to panel (x=0, y=PANEL_H-1) -- that is rotation 270.
  // Turning it the other way is rotation 90; both are offered in the admin page
  // because only the person mounting it knows which way round it sits.
  if (rot == ROTATION_AUTO) rot = (src.w > src.h) ? 270 : 0;
  rot = ((rot % 360) + 360) % 360;
  if (rot != 0 && rot != 90 && rot != 180 && rot != 270) rot = 0;

  const bool swapped = (rot == 90 || rot == 270);
  const int LW = swapped ? PANEL_H : PANEL_W;
  const int LH = swapped ? PANEL_W : PANEL_H;

  // ---- 2. fit the source onto the logical canvas ---------------------------
  const float kx = (float)LW / (float)src.w;
  const float ky = (float)LH / (float)src.h;
  const float scale = (g_cfg.fit == FIT_COVER) ? max(kx, ky) : min(kx, ky);
  const float drawW = src.w * scale;
  const float drawH = src.h * scale;
  const float offX = (LW - drawW) * 0.5f;   // negative when COVER crops
  const float offY = (LH - drawH) * 0.5f;

  LOG.printf("[render] src=%dx%d rot=%d canvas=%dx%d fit=%s scale=%.3f off=(%.1f,%.1f)\n",
             src.w, src.h, rot, LW, LH,
             g_cfg.fit == FIT_COVER ? "cover" : "contain", scale, offX, offY);

  // ---- 3. precompute the horizontal mapping (identical for every row) ------
  // Separable bilinear: the x weights never change, so compute them once
  // instead of 1.92 M times.
  int16_t* sx0  = (int16_t*)malloc((size_t)LW * sizeof(int16_t));
  uint8_t* sxf  = (uint8_t*)malloc((size_t)LW);        // fractional weight, 0..255
  uint8_t* inX  = (uint8_t*)malloc((size_t)LW);        // 0 = letterbox column
  uint8_t* rgbRow  = (uint8_t*)malloc((size_t)LW * 3);
  uint8_t* codeRow = (uint8_t*)malloc((size_t)LW);
  if (!sx0 || !sxf || !inX || !rgbRow || !codeRow) {
    LOG.println("[render] OOM on row scratch buffers");
    free(sx0); free(sxf); free(inX); free(rgbRow); free(codeRow);
    return false;
  }

  const float invScale = 1.0f / scale;
  for (int lx = 0; lx < LW; ++lx) {
    const float fx = ((lx + 0.5f) - offX) * invScale - 0.5f;
    if (fx <= -1.0f || fx >= (float)src.w) { inX[lx] = 0; sx0[lx] = 0; sxf[lx] = 0; continue; }
    int x0 = (int)floorf(fx);
    float fr = fx - x0;
    if (x0 < 0) { x0 = 0; fr = 0.0f; }
    if (x0 > src.w - 1) { x0 = src.w - 1; fr = 0.0f; }
    inX[lx] = 1;
    sx0[lx] = (int16_t)x0;
    sxf[lx] = (uint8_t)(fr * 255.0f + 0.5f);
  }

  // ---- 4. dither + emit, one logical row at a time -------------------------
  E6Ditherer dith;
  if (!dith.begin(LW, g_cfg.dither, g_cfg.gamma)) {
    LOG.println("[render] WARN: error-diffusion buffer alloc failed, fell back to Bayer8");
  }
  frameClear(frame);

  for (int ly = 0; ly < LH; ++ly) {
    const float fy = ((ly + 0.5f) - offY) * invScale - 0.5f;
    const bool rowInside = (fy > -1.0f && fy < (float)src.h);

    if (!rowInside) {
      // Pure letterbox row: feed white so the ditherer stays in step (its error
      // state is per-row) but nothing is drawn.
      memset(rgbRow, 0xFF, (size_t)LW * 3);
    } else {
      int y0 = (int)floorf(fy);
      float fr = fy - y0;
      if (y0 < 0) { y0 = 0; fr = 0.0f; }
      if (y0 > src.h - 1) { y0 = src.h - 1; fr = 0.0f; }
      const int y1 = (y0 + 1 < src.h) ? y0 + 1 : y0;
      const int wy = (int)(fr * 255.0f + 0.5f);

      const uint16_t* row0 = src.px + (size_t)y0 * src.w;
      const uint16_t* row1 = src.px + (size_t)y1 * src.w;

      for (int lx = 0; lx < LW; ++lx) {
        uint8_t* o = rgbRow + lx * 3;
        if (!inX[lx]) { o[0] = o[1] = o[2] = 255; continue; }

        const int x0 = sx0[lx];
        const int x1 = (x0 + 1 < src.w) ? x0 + 1 : x0;
        const int wx = sxf[lx];

        int r00, g00, b00, r01, g01, b01, r10, g10, b10, r11, g11, b11;
        unpack565(row0[x0], &r00, &g00, &b00);
        unpack565(row0[x1], &r01, &g01, &b01);
        unpack565(row1[x0], &r10, &g10, &b10);
        unpack565(row1[x1], &r11, &g11, &b11);

        // Bilinear in 8.8 fixed point: top/bottom lerp on x, then lerp on y.
        const int rt = r00 + (((r01 - r00) * wx) >> 8);
        const int gt = g00 + (((g01 - g00) * wx) >> 8);
        const int bt = b00 + (((b01 - b00) * wx) >> 8);
        const int rb = r10 + (((r11 - r10) * wx) >> 8);
        const int gb = g10 + (((g11 - g10) * wx) >> 8);
        const int bb = b10 + (((b11 - b10) * wx) >> 8);

        o[0] = (uint8_t)(rt + (((rb - rt) * wy) >> 8));
        o[1] = (uint8_t)(gt + (((gb - gt) * wy) >> 8));
        o[2] = (uint8_t)(bt + (((bb - bt) * wy) >> 8));
      }
    }

    dith.row(rgbRow, codeRow);

    // ---- map the logical row onto the panel framebuffer --------------------
    switch (rot) {
      case 0: {
        // Fast path: a logical row is a panel row, so pack two pixels per byte
        // and walk the row linearly.
        uint8_t* b = frame + (size_t)ly * PANEL_STRIDE;
        for (int lx = 0; lx < LW; lx += 2)
          *b++ = (uint8_t)((codeRow[lx] << 4) | codeRow[lx + 1]);
        break;
      }
      case 180: {
        uint8_t* b = frame + (size_t)(LH - 1 - ly) * PANEL_STRIDE;
        for (int lx = 0; lx < LW; lx += 2)
          b[(LW - 2 - lx) >> 1] = (uint8_t)((codeRow[lx + 1] << 4) | codeRow[lx]);
        break;
      }
      case 90: {
        // Logical row -> panel column. px is constant for the whole row, so the
        // high/low nibble choice is decided once rather than per pixel.
        const int px = LH - 1 - ly;
        uint8_t* b = frame + (px >> 1);
        if (px & 1) {
          for (int lx = 0; lx < LW; ++lx, b += PANEL_STRIDE)
            *b = (uint8_t)((*b & 0xF0) | codeRow[lx]);
        } else {
          for (int lx = 0; lx < LW; ++lx, b += PANEL_STRIDE)
            *b = (uint8_t)((*b & 0x0F) | (codeRow[lx] << 4));
        }
        break;
      }
      case 270: {
        const int px = ly;
        uint8_t* b = frame + (size_t)(LW - 1) * PANEL_STRIDE + (px >> 1);
        if (px & 1) {
          for (int lx = 0; lx < LW; ++lx, b -= PANEL_STRIDE)
            *b = (uint8_t)((*b & 0xF0) | codeRow[lx]);
        } else {
          for (int lx = 0; lx < LW; ++lx, b -= PANEL_STRIDE)
            *b = (uint8_t)((*b & 0x0F) | (codeRow[lx] << 4));
        }
        break;
      }
    }
  }

  dith.end();
  free(sx0); free(sxf); free(inX); free(rgbRow); free(codeRow);

  if (st) {
    st->rotation = rot;
    st->logicalW = LW;
    st->logicalH = LH;
    st->ms = millis() - t0;
  }
  LOG.printf("[render] done in %lu ms (dither=%s gamma=%.2f)\n",
             (unsigned long)(millis() - t0), ditherName(g_cfg.dither), g_cfg.gamma);
  return true;
}
