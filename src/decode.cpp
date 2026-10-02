#include "decode.h"
#include "decode_internal.h"
#include "log.h"

#include <SD.h>

// ---------------------------------------------------------------------------
// Shared file access. Both decoders take plain C function pointers, so the
// handle lives here as a file static; decoding is never concurrent.
// ---------------------------------------------------------------------------
static File s_file;

void* decFileOpen(const char* filename, int32_t* size) {
  if (s_file) s_file.close();   // never leak a handle if a decode was aborted
  s_file = SD.open(filename, FILE_READ);
  if (!s_file) return nullptr;
  *size = (int32_t)s_file.size();
  return (void*)&s_file;
}

void decFileClose(void* handle) {
  (void)handle;
  if (s_file) s_file.close();
}

int32_t decFileRead(uint8_t* buf, int32_t len) {
  if (!s_file) return 0;
  return (int32_t)s_file.read(buf, len);
}

int32_t decFileSeek(int32_t pos) {
  if (!s_file) return 0;
  return s_file.seek(pos) ? pos : 0;
}

bool srcAlloc(SrcImage* out, int w, int h, size_t budget) {
  const size_t need = (size_t)w * h * 2;
  if (need > budget) {
    LOG.printf("[dec] %dx%d needs %u kB > budget %u kB\n", w, h,
               (unsigned)(need / 1024), (unsigned)(budget / 1024));
    return false;
  }
  out->px = (uint16_t*)ps_malloc(need);
  if (!out->px) out->px = (uint16_t*)malloc(need);
  if (!out->px) {
    LOG.printf("[dec] alloc of %u kB FAILED\n", (unsigned)(need / 1024));
    return false;
  }
  out->w = w;
  out->h = h;
  memset(out->px, 0xFF, need);   // white, so unwritten areas read as paper
  return true;
}

// ---------------------------------------------------------------------------
// Failure detail. A DecodeResult is a category; this is the sentence that says
// what actually happened, so the admin page can stop guessing.
static char s_detail[112] = "";

void decSetDetail(const char* s) {
  if (!s) { s_detail[0] = 0; return; }
  strncpy(s_detail, s, sizeof(s_detail) - 1);
  s_detail[sizeof(s_detail) - 1] = 0;
}

const char* decodeLastDetail() { return s_detail; }

// ---------------------------------------------------------------------------
void srcFree(SrcImage* img) {
  if (!img) return;
  if (img->px) free(img->px);
  img->px = nullptr;
  img->w = img->h = 0;
}

const char* decodeResultName(DecodeResult r) {
  switch (r) {
    case DEC_OK:               return "OK";
    case DEC_IO:               return "FILE ERROR";
    case DEC_TOO_BIG:          return "IMAGE TOO LARGE";
    case DEC_OOM:              return "OUT OF MEMORY";
    case DEC_CORRUPT:          return "DECODE FAILED";
    case DEC_UNSUPPORTED_WEBP: return "WEBP NOT SUPPORTED";
    case DEC_UNSUPPORTED:      return "FORMAT NOT SUPPORTED";
  }
  return "?";
}

const char* decodeResultHint(DecodeResult r) {
  switch (r) {
    case DEC_UNSUPPORTED_WEBP:
      return "Set Immich's thumbnail format to JPEG "
             "(Administration > Settings > Image Settings), or pick a different image size.";
    case DEC_TOO_BIG:
      return "Choose a smaller image size (preview instead of original) in the admin page.";
    case DEC_CORRUPT:
      return "The decoder rejected this image. Baseline JPEG and PNG only -- "
             "progressive JPEG is not supported.";
    case DEC_UNSUPPORTED:
      return "Only JPEG and PNG are supported. Use the 'preview' image size.";
    default:
      return "";
  }
}

// Format is decided by magic bytes, not by the extension: Immich serves images
// from an endpoint with no filename at all.
namespace {
enum Fmt { FMT_JPEG, FMT_PNG, FMT_WEBP, FMT_BMP, FMT_UNKNOWN };

Fmt sniff(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return FMT_UNKNOWN;
  uint8_t h[16] = {0};
  const size_t n = f.read(h, sizeof(h));
  f.close();
  if (n < 4) return FMT_UNKNOWN;
  if (h[0] == 0xFF && h[1] == 0xD8) return FMT_JPEG;
  if (h[0] == 0x89 && h[1] == 'P' && h[2] == 'N' && h[3] == 'G') return FMT_PNG;
  if (n >= 12 && memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WEBP", 4) == 0) return FMT_WEBP;
  if (h[0] == 'B' && h[1] == 'M') return FMT_BMP;
  return FMT_UNKNOWN;
}
}  // namespace

DecodeResult decodeToRgb565(const char* path, size_t psramBudget, SrcImage* out) {
  srcFree(out);
  decSetDetail(nullptr);
  switch (sniff(path)) {
    case FMT_JPEG: return decodeJpegFile(path, psramBudget, out);
    case FMT_PNG:  return decodePngFile(path, psramBudget, out);
    case FMT_WEBP: LOG.println("[dec] WebP is not supported"); return DEC_UNSUPPORTED_WEBP;
    case FMT_BMP:  LOG.println("[dec] BMP is not supported");  return DEC_UNSUPPORTED;
    default:       LOG.println("[dec] unrecognised file header"); return DEC_UNSUPPORTED;
  }
}
