#pragma once
#include <Arduino.h>

// A decoded source photo held in PSRAM as RGB565 little-endian.
//
// RGB565 rather than RGB888 is a deliberate choice: it halves the biggest
// allocation in the whole firmware, and the panel only has SIX colours, so the
// 5/6/5 quantisation is utterly invisible after dithering.
struct SrcImage {
  uint16_t* px = nullptr;
  int w = 0;
  int h = 0;
  size_t bytes() const { return (size_t)w * h * 2; }
};

enum DecodeResult {
  DEC_OK = 0,
  DEC_IO,             // cannot open / read the file
  DEC_TOO_BIG,        // will not fit in PSRAM even at the smallest decode scale
  DEC_OOM,            // allocation failed
  DEC_CORRUPT,        // decoder rejected the data
  DEC_UNSUPPORTED_WEBP,
  DEC_UNSUPPORTED,    // some other format (BMP, HEIC, ...)
};

const char* decodeResultName(DecodeResult r);
const char* decodeResultHint(DecodeResult r);

// Extra detail about the most recent decode failure -- for JPEG this names the
// exact feature the decoder choked on, which the DecodeResult alone cannot say.
// Empty when there is nothing to add. Valid until the next decodeToRgb565().
const char* decodeLastDetail();

void srcFree(SrcImage* img);

// Decode `path` (on the mounted SD card) into `out`.
//
// `psramBudget` caps the pixel buffer. For JPEG we exploit JPEGDEC's
// decode-time 1/2, 1/4 and 1/8 downscale and pick the highest quality that
// fits, which is what stops a 48 MP original from OOM-ing the device. PNG has
// no decode-time downscale, so an oversized PNG returns DEC_TOO_BIG with a
// useful message instead of rebooting.
DecodeResult decodeToRgb565(const char* path, size_t psramBudget, SrcImage* out);
