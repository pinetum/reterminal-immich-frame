#include "decode_internal.h"
#include "log.h"

#include <JPEGDEC.h>
#include <SD.h>

namespace {

JPEGDEC   g_jpeg;
SrcImage* g_out = nullptr;

int32_t onRead(JPEGFILE* f, uint8_t* buf, int32_t len) { (void)f; return decFileRead(buf, len); }
int32_t onSeek(JPEGFILE* f, int32_t pos)               { (void)f; return decFileSeek(pos); }

// ---------------------------------------------------------------------------
// Header probe.
//
// JPEGDEC reports a single error code (JPEG_UNSUPPORTED_FEATURE == 3) for
// several unrelated reasons, so an error number on its own cannot tell the user
// which Immich setting to change. This walks the marker segments ourselves and
// applies exactly the acceptance tests JPEGDEC 1.8 performs, so the log names
// the real cause instead of always blaming progressive encoding.
//
// The tests, from JPEGParseInfo() and JPEGMakeHuffTables():
//   * SOF0 (baseline) and SOF1 (extended sequential) decode -- the latter only
//     because of the patch in lib/JPEGDEC/PATCHES.md, which is what makes
//     Immich's mozjpeg previews work. SOF3 is rejected outright and SOF2
//     (progressive) gets as far as the first scan and then fails.
//   * 8-bit samples, 1 or 3 components, Y sampling 1x1 / 2x1 / 1x2 / 2x2.
//   * Huffman codes have to fit JPEGDEC's split lookup tables: DC codes of more
//     than 6 bits and AC codes of more than 10 bits are only representable when
//     they fall in the "leading ones" long-table bucket (5 ones for DC, 6 for
//     AC). Encoders that optimise their Huffman tables can emit codes that do
//     not, and those images fail while every other photo in the album works.
// ---------------------------------------------------------------------------

struct JpegHeader {
  uint8_t sof   = 0;      // SOF marker byte, 0 if none was found
  uint8_t prec  = 0;
  uint8_t ncomp = 0;
  uint8_t samp  = 0;      // h<<4 | v of the first (Y) component
  int     w     = 0;
  int     h     = 0;
  char    reason[96] = "";  // empty => nothing JPEGDEC objects to
};

const char* sofName(uint8_t m) {
  switch (m) {
    case 0xC0: return "baseline";
    case 0xC1: return "extended sequential";
    case 0xC2: return "progressive";
    case 0xC3: return "lossless";
    case 0xC9: case 0xCA: case 0xCB: return "arithmetic-coded";
    default:   return "unsupported frame type";
  }
}

// Walk the canonical Huffman codes described by bits[0..15] and check each one
// against the bucket JPEGDEC would store it in. `ac` picks the AC rules.
// Returns the offending code length, or 0 if every code fits.
int huffUnfit(const uint8_t bits[16], bool ac) {
  uint32_t cc = 0;                       // canonical code, built up by length
  for (int n = 1; n <= 16; ++n) {
    for (int k = 0; k < bits[n - 1]; ++k, ++cc) {
      if (!ac && n > 12) return n;       // DC codes are capped at 12 bits
      const int lead = ac ? 6 : 5;       // leading 1s that select the long table
      if (n >= lead && (cc >> (n - lead)) == (uint32_t)((1u << lead) - 1)) continue;
      if (n > (ac ? 10 : 6)) return n;   // short table cannot hold it
    }
    cc <<= 1;
  }
  return 0;
}

// Read the header of `path` without disturbing the shared decoder file handle.
bool scanJpegHeader(const char* path, JpegHeader* out) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  uint8_t b[16];
  if (f.read(b, 2) != 2 || b[0] != 0xFF || b[1] != 0xD8) { f.close(); return false; }

  while (true) {
    int c = f.read();
    if (c < 0) break;
    if (c != 0xFF) continue;             // resync: marker segments are byte-aligned
    int m = f.read();
    while (m == 0xFF) m = f.read();      // fill bytes
    if (m < 0) break;
    if (m == 0x00 || m == 0x01 || (m >= 0xD0 && m <= 0xD8)) continue;
    if (m == 0xD9 || m == 0xDA) break;   // EOI, or SOS: the header is complete
    if (f.read(b, 2) != 2) break;
    const int len = (b[0] << 8) | b[1];
    if (len < 2) break;
    const uint32_t segEnd = f.position() + (uint32_t)(len - 2);

    if ((m >= 0xC0 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) {
      if (f.read(b, 6) != 6) break;
      out->sof   = (uint8_t)m;
      out->prec  = b[0];
      out->h     = (b[1] << 8) | b[2];
      out->w     = (b[3] << 8) | b[4];
      out->ncomp = b[5];
      if (out->ncomp >= 1 && f.read(b, 3) == 3) out->samp = b[1];

      if (m != 0xC0 && m != 0xC1)
        snprintf(out->reason, sizeof(out->reason), "%s JPEG (SOF%d) is not supported",
                 sofName((uint8_t)m), m - 0xC0);
      else if (out->prec != 8)
        snprintf(out->reason, sizeof(out->reason), "%u-bit samples are not supported",
                 (unsigned)out->prec);
      else if (out->ncomp != 1 && out->ncomp != 3)
        snprintf(out->reason, sizeof(out->reason),
                 "%u colour components (only greyscale and YCbCr are supported)",
                 (unsigned)out->ncomp);
      else if (out->ncomp == 3 && out->samp != 0x11 && out->samp != 0x21 &&
               out->samp != 0x12 && out->samp != 0x22)
        snprintf(out->reason, sizeof(out->reason),
                 "%dx%d chroma subsampling is not supported",
                 out->samp >> 4, out->samp & 0x0F);
    } else if (m == 0xC4) {              // DHT, possibly several tables in one segment
      while (f.position() + 17 <= segEnd) {
        const int tc = f.read();
        if (tc < 0) break;
        uint8_t bits[16];
        if (f.read(bits, 16) != 16) break;
        int nvals = 0;
        for (int i = 0; i < 16; ++i) nvals += bits[i];
        const bool ac = (tc & 0x10) != 0;
        const int  id = tc & 0x0F;
        if (!out->reason[0]) {
          if (const int bad = huffUnfit(bits, ac))
            snprintf(out->reason, sizeof(out->reason),
                     "%s Huffman table %d uses a %d-bit code the decoder cannot index",
                     ac ? "AC" : "DC", id, bad);
        }
        f.seek(f.position() + nvals);
      }
    }
    f.seek(segEnd);
  }
  f.close();
  return out->sof != 0;
}

// JPEGDEC hands us MCU blocks in raster order. Blocks on the right and bottom
// edges are padded out to the MCU grid, so every copy has to be clipped.
int onDraw(JPEGDRAW* pDraw) {
  if (!g_out || !g_out->px) return 0;      // 0 aborts the decode
  const int W = g_out->w, H = g_out->h;
  for (int row = 0; row < pDraw->iHeight; ++row) {
    const int dy = pDraw->y + row;
    if (dy < 0 || dy >= H) continue;
    int n = pDraw->iWidth;
    if (pDraw->x + n > W) n = W - pDraw->x;
    if (n <= 0) continue;
    memcpy(g_out->px + (size_t)dy * W + pDraw->x,
           pDraw->pPixels + (size_t)row * pDraw->iWidth,
           (size_t)n * sizeof(uint16_t));
  }
  return 1;
}

}  // namespace

DecodeResult decodeJpegFile(const char* path, size_t budget, SrcImage* out) {
  // Probe first: it costs one pass over a few kB of header and it is the only
  // way to say anything useful when JPEGDEC refuses the file.
  JpegHeader hdr;
  if (scanJpegHeader(path, &hdr)) {
    LOG.printf("[dec] JPEG %dx%d  %s  %u-bit  %u comp  Y %dx%d\n", hdr.w, hdr.h,
               sofName(hdr.sof), (unsigned)hdr.prec, (unsigned)hdr.ncomp,
               hdr.samp >> 4, hdr.samp & 0x0F);
    if (hdr.reason[0]) {
      LOG.printf("[dec] cannot decode: %s\n", hdr.reason);
      decSetDetail(hdr.reason);
      return DEC_CORRUPT;
    }
  }

  if (!g_jpeg.open(path, decFileOpen, decFileClose, onRead, onSeek, onDraw)) {
    const int e = g_jpeg.getLastError();
    LOG.printf("[dec] JPEG header rejected by JPEGDEC (err %d)\n", e);
    decSetDetail(e == JPEG_UNSUPPORTED_FEATURE ? "the decoder rejected this file's JPEG header"
                                               : "malformed JPEG header");
    return DEC_CORRUPT;
  }
  const int fw = g_jpeg.getWidth();
  const int fh = g_jpeg.getHeight();

  // Pick the highest-quality decode scale that fits the budget. This is the
  // guard that makes `size=original` safe on a 48 MP photo: without it the RGB565
  // buffer for a 8000x6000 image would be 96 MB.
  static const struct { int div; int opt; } kScales[] = {
      {1, 0}, {2, JPEG_SCALE_HALF}, {4, JPEG_SCALE_QUARTER}, {8, JPEG_SCALE_EIGHTH},
  };
  int div = 0, opt = 0, dw = 0, dh = 0;
  for (const auto& s : kScales) {
    const int w = (fw + s.div - 1) / s.div;
    const int h = (fh + s.div - 1) / s.div;
    if ((size_t)w * h * 2 <= budget) { div = s.div; opt = s.opt; dw = w; dh = h; break; }
  }
  if (!div) {
    LOG.printf("[dec] JPEG %dx%d too large even at 1/8 scale\n", fw, fh);
    g_jpeg.close();
    return DEC_TOO_BIG;
  }
  LOG.printf("[dec] JPEG %dx%d -> decoding at 1/%d = %dx%d (%u kB)\n",
             fw, fh, div, dw, dh, (unsigned)((size_t)dw * dh * 2 / 1024));

  if (!srcAlloc(out, dw, dh, budget)) { g_jpeg.close(); return DEC_OOM; }

  g_out = out;
  g_jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  const int rc = g_jpeg.decode(0, 0, opt);
  g_jpeg.close();
  g_out = nullptr;

  if (rc != 1) {
    const int e = g_jpeg.getLastError();
    LOG.printf("[dec] JPEG decode failed (err %d)\n", e);
    decSetDetail(e == JPEG_UNSUPPORTED_FEATURE
                     ? "the scan uses a JPEG feature the decoder does not implement"
                     : "the compressed data is truncated or corrupt");
    srcFree(out);
    return DEC_CORRUPT;
  }
  return DEC_OK;
}
