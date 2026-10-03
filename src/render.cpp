#include "render.h"
#include "settings.h"
#include "log.h"
#include "e6_dither.h"
#include "imgproc.h"
#include "palette.h"

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

// Resolved geometry, shared between renderFrame() and renderPreview().
struct Geometry {
  int   rot, LW, LH;
  float scale, offX, offY;
};

Geometry resolveGeometry(const SrcImage& src) {
  Geometry g;

  // The panel framebuffer is always panel-native portrait 1200x1600.
  // "Rotating" means composing onto a logical canvas and mapping its
  // coordinates onto the panel; the dither error still diffuses in logical scan
  // order, which is the order the eye reads the picture in. That is the correct
  // place to rotate.
  int rot = g_cfg.rotation;
  // AUTO assumes the frame is turned CLOCKWISE for landscape viewing, which maps
  // the viewer's top-left to panel (x=0, y=PANEL_H-1) -- that is rotation 270.
  // Turning it the other way is rotation 90; both are offered in the admin page
  // because only the person mounting it knows which way round it sits.
  if (rot == ROTATION_AUTO) rot = (src.w > src.h) ? 270 : 0;
  rot = ((rot % 360) + 360) % 360;
  if (rot != 0 && rot != 90 && rot != 180 && rot != 270) rot = 0;

  const bool swapped = (rot == 90 || rot == 270);
  g.rot = rot;
  g.LW  = swapped ? PANEL_H : PANEL_W;
  g.LH  = swapped ? PANEL_W : PANEL_H;

  const float kx = (float)g.LW / (float)src.w;
  const float ky = (float)g.LH / (float)src.h;
  g.scale = (g_cfg.fit == FIT_COVER) ? max(kx, ky) : min(kx, ky);
  g.offX  = (g.LW - src.w * g.scale) * 0.5f;   // negative when COVER crops
  g.offY  = (g.LH - src.h * g.scale) * 0.5f;
  return g;
}

// The part of the source that actually reaches the panel, in source pixels.
// Under FIT_COVER this is a strict subset: handing the discarded edges to the
// percentile analysis would set the dynamic range from tones nobody sees.
ImgProcRect visibleRect(const SrcImage& src, const Geometry& g) {
  const float inv = 1.0f / g.scale;
  const float fx0 = (0.5f - g.offX) * inv - 0.5f;
  const float fx1 = ((g.LW - 0.5f) - g.offX) * inv - 0.5f;
  const float fy0 = (0.5f - g.offY) * inv - 0.5f;
  const float fy1 = ((g.LH - 0.5f) - g.offY) * inv - 0.5f;

  ImgProcRect r;
  r.x0 = (int)max(0.0f, floorf(fx0));
  r.y0 = (int)max(0.0f, floorf(fy0));
  r.x1 = (int)min((float)(src.w - 1), ceilf(fx1));
  r.y1 = (int)min((float)(src.h - 1), ceilf(fy1));
  if (r.x1 < r.x0) r.x1 = r.x0;
  if (r.y1 < r.y0) r.y1 = r.y0;
  return r;
}

// One logical row of nibble codes mapped into the packed 4bpp panel frame.
void emitRow(uint8_t* frame, int rot, int ly, int LW, int LH, const uint8_t* codeRow) {
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

bool renderFrame(SrcImage& src, uint8_t* frame, RenderStats* st) {
  if (!src.px || src.w < 1 || src.h < 1 || !frame) return false;
  buildLuts();
  const uint32_t t0 = millis();

  const E6Palette& pal = settingsPalette(g_cfg);
  const Geometry   g   = resolveGeometry(src);
  const int LW = g.LW, LH = g.LH;

  LOG.printf("[render] src=%dx%d rot=%d canvas=%dx%d fit=%s scale=%.3f off=(%.1f,%.1f) palette=%s\n",
             src.w, src.h, g.rot, LW, LH,
             g_cfg.fit == FIT_COVER ? "cover" : "contain", g.scale, g.offX, g.offY,
             paletteIdName(g_cfg.paletteId));

  // ---- 1. source-space processing (gamma, paper normalisation, clarity) ----
  // These either need a 2D neighbourhood or have to run before scaling, so
  // they rewrite the decoded source in place. Skipped entirely -- no
  // allocation, no pass -- when all three are neutral, which is the default.
  bool clarityDropped = false;
  const uint32_t tSrc0 = millis();
  if (imgprocSourcePassNeeded(g_cfg.proc, g_cfg.gamma)) {
    if (!imgprocSourcePass(src.px, src.w, src.h, g_cfg.proc, g_cfg.gamma, g.scale)) {
      clarityDropped = true;
      LOG.println("[render] WARN: clarity scratch alloc failed -- clarity skipped this frame");
    }
  }
  const uint32_t tSrc = millis() - tSrc0;

  // ---- 2. resolve the lookup tables and the percentile-driven auto modes ---
  const uint32_t tAna0 = millis();
  ImgProcPlan plan;
  imgprocAnalyze(src.px, src.w, src.h, visibleRect(src, g), pal, g_cfg.proc, &plan);
  const uint32_t tAna = millis() - tAna0;

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

  const float invScale = 1.0f / g.scale;
  bool anyLetterboxCol = false;
  for (int lx = 0; lx < LW; ++lx) {
    const float fx = ((lx + 0.5f) - g.offX) * invScale - 0.5f;
    if (fx <= -1.0f || fx >= (float)src.w) {
      inX[lx] = 0; sx0[lx] = 0; sxf[lx] = 0; anyLetterboxCol = true; continue;
    }
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
  if (!dith.begin(LW, pal, g_cfg.dither)) {
    LOG.println("[render] WARN: error-diffusion buffer alloc failed, fell back to ordered");
  }
  frameClear(frame);

  // Letterbox bars are fed the palette's own white rather than 0xFFFFFF. That
  // makes the nearest-colour distance exactly zero, so the bar quantises to
  // white with no residual error diffusing into the picture beside it. With a
  // calibrated palette, feeding pure white would push a real error into the
  // first columns of the photo.
  const int      whiteSlot = paletteLightest(pal);
  const uint8_t* whiteRgb  = pal.rgb[whiteSlot];

  const uint32_t tRow0 = millis();
  for (int ly = 0; ly < LH; ++ly) {
    const float fy = ((ly + 0.5f) - g.offY) * invScale - 0.5f;
    const bool rowInside = (fy > -1.0f && fy < (float)src.h);

    if (!rowInside) {
      // Pure letterbox row: feed the palette white so the ditherer stays in
      // step (its error state is per-row) but nothing is drawn. Skips the
      // processing stages, which would otherwise tone-map the border.
      for (int lx = 0; lx < LW; ++lx) {
        rgbRow[lx * 3 + 0] = whiteRgb[0];
        rgbRow[lx * 3 + 1] = whiteRgb[1];
        rgbRow[lx * 3 + 2] = whiteRgb[2];
      }
      dith.row(rgbRow, codeRow);
      emitRow(frame, g.rot, ly, LW, LH, codeRow);
      continue;
    }

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
      if (!inX[lx]) { o[0] = o[1] = o[2] = 0; continue; }   // filled in below

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

    // Pointwise processing: exposure, saturation, tone, dynamic range, level.
    imgprocRow(plan, rgbRow, LW);

    // Letterbox columns are painted after processing, for the same reason the
    // letterbox rows skip it.
    if (anyLetterboxCol) {
      for (int lx = 0; lx < LW; ++lx) {
        if (inX[lx]) continue;
        rgbRow[lx * 3 + 0] = whiteRgb[0];
        rgbRow[lx * 3 + 1] = whiteRgb[1];
        rgbRow[lx * 3 + 2] = whiteRgb[2];
      }
    }

    dith.row(rgbRow, codeRow);
    emitRow(frame, g.rot, ly, LW, LH, codeRow);
  }
  const uint32_t tRows = millis() - tRow0;

  dith.end();
  free(sx0); free(sxf); free(inX); free(rgbRow); free(codeRow);

  if (st) {
    st->rotation = g.rot;
    st->logicalW = LW;
    st->logicalH = LH;
    st->ms = millis() - t0;
    st->msSourcePass = tSrc;
    st->msAnalyze = tAna;
    st->msRows = tRows;
    st->clarityDropped = clarityDropped;
  }
  LOG.printf("[render] done in %lu ms (source %lu + analyze %lu + rows %lu) "
             "dither=%s/%s match=%s gamma=%.2f\n",
             (unsigned long)(millis() - t0), (unsigned long)tSrc, (unsigned long)tAna,
             (unsigned long)tRows, ditherTypeName(g_cfg.dither.type),
             edMatrixName(g_cfg.dither.matrix), colorMatchName(g_cfg.dither.matching),
             g_cfg.gamma);
  return true;
}

bool renderPreview(SrcImage& src, uint8_t* out, RenderStats* st) {
  if (!out) return false;
  uint8_t* frame = frameAlloc();
  if (!frame) return false;

  if (!renderFrame(src, frame, st)) {
    frameFree(frame);
    return false;
  }

  const E6Palette& pal = settingsPalette(g_cfg);

  // Palette RGB per nibble code, so the averaging loop is three table lookups
  // rather than a six-way search per pixel.
  uint8_t codeRgb[16][3];
  for (int c = 0; c < 16; ++c)
    e6CodeToRgb(pal, (uint8_t)c, &codeRgb[c][0], &codeRgb[c][1], &codeRgb[c][2]);

  const int block = PREVIEW_DIV * PREVIEW_DIV;
  for (int py = 0; py < PREVIEW_H; ++py) {
    for (int px = 0; px < PREVIEW_W; ++px) {
      uint32_t acc[3] = {0, 0, 0};
      for (int dy = 0; dy < PREVIEW_DIV; ++dy) {
        const uint8_t* srow = frame + (size_t)(py * PREVIEW_DIV + dy) * PANEL_STRIDE;
        for (int dx = 0; dx < PREVIEW_DIV; ++dx) {
          const int x = px * PREVIEW_DIV + dx;
          const uint8_t byte = srow[x >> 1];
          const uint8_t code = (x & 1) ? (byte & 0x0F) : (byte >> 4);
          acc[0] += codeRgb[code][0];
          acc[1] += codeRgb[code][1];
          acc[2] += codeRgb[code][2];
        }
      }
      uint8_t* o = out + ((size_t)py * PREVIEW_W + px) * 3;
      o[0] = (uint8_t)(acc[0] / block);
      o[1] = (uint8_t)(acc[1] / block);
      o[2] = (uint8_t)(acc[2] / block);
    }
  }

  frameFree(frame);
  return true;
}

void renderPaletteChart(uint8_t* frame) {
  if (!frame) return;
  frameClear(frame);

  // Six horizontal bands, panel-native orientation, each a solid ink. Solid
  // rather than dithered on purpose: the point is to photograph one pure ink
  // at a time and read its RGB, which a dither pattern would average away.
  const int bandH = PANEL_H / PAL_SLOTS;
  for (int slot = 0; slot < PAL_SLOTS; ++slot) {
    const uint8_t code = e6SlotCode(slot);
    const uint8_t pair = (uint8_t)((code << 4) | code);
    const int yEnd = (slot == PAL_SLOTS - 1) ? PANEL_H : (slot + 1) * bandH;
    for (int y = slot * bandH; y < yEnd; ++y)
      memset(frame + (size_t)y * PANEL_STRIDE, pair, PANEL_STRIDE);
  }
}
