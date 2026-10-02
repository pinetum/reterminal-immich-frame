#pragma once
#include "decode.h"

// JPEGDEC and PNGdec both define helper macros with the same names
// (INTELSHORT / MOTOSHORT / MOTOLONG / ...) but different definitions, so
// including both headers in one translation unit produces a pile of redefinition
// warnings. They therefore live in separate .cpp files, and this header carries
// the little they share.

// One file handle, owned by decode.cpp, used by whichever decoder is running.
// Decoding is never concurrent, so a single handle is correct and avoids each
// decoder keeping its own.
void*   decFileOpen(const char* filename, int32_t* size);
void    decFileClose(void* handle);
int32_t decFileRead(uint8_t* buf, int32_t len);
int32_t decFileSeek(int32_t pos);

// Allocate the RGB565 pixel buffer in PSRAM and fill it white, so any area the
// decoder never writes (MCU padding, a truncated file) reads as paper.
bool srcAlloc(SrcImage* out, int w, int h, size_t budget);

// Record a one-line, human-readable detail about the current failure (which
// JPEG feature was rejected, say). Surfaced by decodeLastDetail().
void decSetDetail(const char* s);

DecodeResult decodeJpegFile(const char* path, size_t budget, SrcImage* out);
DecodeResult decodePngFile(const char* path, size_t budget, SrcImage* out);
