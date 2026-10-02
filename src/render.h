#pragma once
#include <Arduino.h>
#include "decode.h"
#include "pins.h"

struct RenderStats {
  int rotation = 0;     // 0 / 90 / 180 / 270 actually used
  int logicalW = 0;     // canvas the photo was composed on, before rotation
  int logicalH = 0;
  uint32_t ms = 0;
};

// Allocate / release the panel-native packed-4bpp frame (960 kB, PSRAM).
uint8_t* frameAlloc();
void     frameFree(uint8_t* frame);

// Fill `frame` with white (code 0x0 -> byte 0x00).
void frameClear(uint8_t* frame);

// Scale, rotate and dither `src` straight into the packed 4bpp panel frame.
//
// Everything happens in a single streaming pass over the OUTPUT rows, so the
// only large buffers alive are the decoded source and the frame itself. See
// lib/e6_dither for why that matters.
//
// Reads g_cfg for dither method, gamma, fit mode and rotation.
bool renderFrame(const SrcImage& src, uint8_t* frame, RenderStats* st);
