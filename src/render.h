#pragma once
#include <Arduino.h>
#include "decode.h"
#include "pins.h"

struct RenderStats {
  int rotation = 0;     // 0 / 90 / 180 / 270 actually used
  int logicalW = 0;     // canvas the photo was composed on, before rotation
  int logicalH = 0;
  uint32_t ms = 0;

  // Per-stage cost, so the price of each new processing stage is measurable on
  // the device instead of guessed at. All in milliseconds.
  uint32_t msSourcePass = 0;   // gamma + paper normalisation + clarity
  uint32_t msAnalyze    = 0;   // histogram pass for the "auto" modes
  uint32_t msRows       = 0;   // scale + process + dither + pack
  bool     clarityDropped = false;  // scratch alloc failed, clarity was skipped
};

// Allocate / release the panel-native packed-4bpp frame (960 kB, PSRAM).
uint8_t* frameAlloc();
void     frameFree(uint8_t* frame);

// Fill `frame` with white (code 0x0 -> byte 0x00).
void frameClear(uint8_t* frame);

// Scale, rotate, pre-process and dither `src` straight into the packed 4bpp
// panel frame.
//
// Everything happens in a single streaming pass over the OUTPUT rows, so the
// only large buffers alive are the decoded source and the frame itself. See
// include/e6_dither.h and include/imgproc.h for why that matters.
//
// `src` is taken by non-const reference because the pre-dither stages that
// need a 2D neighbourhood (clarity) or must run before scaling (gamma, paper
// normalisation) rewrite the decoded source in place. Call it once per decode.
//
// Reads g_cfg for the palette, dither configuration, processing settings, fit
// mode and rotation.
bool renderFrame(SrcImage& src, uint8_t* frame, RenderStats* st);

// --- admin-page preview ----------------------------------------------------
// A 1/4-scale preview of what the panel will show.
//
// It is produced by rendering the REAL full-resolution frame and then box-
// averaging each 4x4 block of nibbles through the calibrated palette. That is
// deliberately not the same as dithering at 300x400: the dither grain would
// then be four times too coarse relative to the picture, and the whole point of
// the preview is to judge colour and tone. Averaging the calibrated colours is
// what the eye does at viewing distance, so this is the honest preview.
#define PREVIEW_DIV   4
#define PREVIEW_W     (PANEL_W / PREVIEW_DIV)   // 300
#define PREVIEW_H     (PANEL_H / PREVIEW_DIV)   // 400
#define PREVIEW_BYTES ((size_t)PREVIEW_W * PREVIEW_H * 3)

// `out` must hold PREVIEW_BYTES of RGB888. Allocates and frees its own panel
// frame, so it needs ~960 kB of PSRAM free for the duration.
bool renderPreview(SrcImage& src, uint8_t* out, RenderStats* st);

// Draw the six palette inks as solid patches, labelled by position, into a
// panel frame. Photograph the panel, read the six patches with any colour
// picker, and type the values into the admin page's custom palette -- the
// manual equivalent of esp32-photoframe's auto-sampling calibration wizard.
void renderPaletteChart(uint8_t* frame);
