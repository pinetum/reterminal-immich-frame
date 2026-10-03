#include "imgproc.h"
#include "colorspace.h"
#include "e6_dither.h"
#include <math.h>

// =============================================================================
//  Paper-normalisation constants.
//
//  epdoptimize exposes nine knobs here; we expose two (mode and strength) and
//  fix the rest, because the remaining seven only ever had ONE tuned set of
//  values in the reference -- the `posterScan` preset -- and the library's own
//  in-function defaults are the untuned ones the preset exists to override.
//  Shipping seven sliders whose only known-good configuration is a single point
//  would be giving the user rope, not control.
// =============================================================================
namespace {

constexpr float kPaperMinLuma      = 82.0f;
constexpr float kPaperSatThreshold = 0.56f;
constexpr float kPaperWarmBias     = 8.0f;
constexpr float kPaperBlackAnchor  = 0.95f;
constexpr float kPaperPreserveRed  = 0.85f;
constexpr uint8_t kPaperWhite[3]   = {248, 248, 246};

// Histograms for the "auto" modes. File-static rather than stack: 2 kB is a
// quarter of the Arduino loop task's stack, and everything slow in this
// firmware runs on that one task (see the comment at the top of src/app.h), so
// there is no reentrancy to worry about.
constexpr int kHistBins = 256;
uint32_t s_histQ[kHistBins];      // DRC quantity, normalised to the domain
uint32_t s_histWhite[kHistBins];  // source luma of low-saturation pixels

// Rounded expansion, matching the g_lut5/g_lut6 tables in src/render.cpp.
// Truncating instead would make the analysis pass see slightly different
// values than the render pass, for no reason.
inline void unpack565(uint16_t v, int* r, int* g, int* b) {
  *r = (((v >> 11) & 0x1F) * 255 + 15) / 31;
  *g = (((v >> 5) & 0x3F) * 255 + 31) / 63;
  *b = ((v & 0x1F) * 255 + 15) / 31;
}

inline uint16_t pack565(int r, int g, int b) {
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// Inverse of csLstarFromY.
inline float yFromLstar(float l) {
  if (l <= 8.0f) return l / 903.3f;
  const float t = (l + 16.0f) / 116.0f;
  return t * t * t;
}

// percentileFromHistogram() in epdoptimize/src/dither/processing.ts: the value
// at rank round((count-1)*p), returned as a bin centre in 0..1.
float percentile(const uint32_t* hist, uint32_t count, float p) {
  if (count == 0) return 0.0f;
  const uint32_t target = (uint32_t)csClampf(roundf((count - 1) * p), 0.0f, (float)(count - 1));
  uint32_t seen = 0;
  for (int i = 0; i < kHistBins; ++i) {
    seen += hist[i];
    if (seen > target) return (float)i / (float)(kHistBins - 1);
  }
  return 1.0f;
}

// buildScurveLookup() in epdoptimize, in the sign convention of
// applyScurveTonemap() in epaper-image-convert (positive highlightCompress).
//
// epdoptimize additionally multiplies the shadow term by a SHADOW_TONE_RESPONSE
// of 1.5 "to make shadow recovery more visible at practical values". We follow
// epaper-image-convert and do not, because our preset values and slider ranges
// come from there -- applying the amplification on top of values tuned without
// it would overshoot. The [0.15, 3] exponent clamp is kept from epdoptimize; it
// is what stops a strength/boost combination from inverting the curve.
void buildScurve(float strength, float shadowBoost, float highlightCompress, float midpoint,
                 uint8_t* lut) {
  const float mid = csClampf(midpoint, 0.01f, 0.99f);
  const float shadowExp    = csClampf(1.0f - strength * shadowBoost, 0.15f, 3.0f);
  const float highlightExp = csClampf(1.0f + strength * highlightCompress, 0.15f, 3.0f);

  for (int v = 0; v < 256; ++v) {
    const float n = v / 255.0f;
    float r;
    if (n <= mid) {
      r = powf(n / mid, shadowExp) * mid;
    } else {
      r = mid + powf((n - mid) / (1.0f - mid), highlightExp) * (1.0f - mid);
    }
    lut[v] = (uint8_t)csClamp8((int)(csClampf(r, 0.0f, 1.0f) * 255.0f + 0.5f));
  }
}

// applySaturation() in both references does a full RGB -> HSL -> RGB round trip
// with a fixed hue and lightness, scaling S and clamping it to 1.
//
// That collapses, exactly, to a lerp of every channel towards the HSL
// lightness L = (max+min)/2. Proof: with H and L fixed, c = (1-|2L-1|)*S and
// x = c*(1-|(H*6 mod 2)-1|) are both linear in S, and the three channels are
// permutations of {c+m, x+m, m} with m = L - c/2, so max = L + c/2,
// min = L - c/2 and mid = L + (x - c/2). Scaling S by f therefore scales each
// channel's distance from L by f. The clamp S' = min(S*k, 1) makes the
// effective factor f = min(k, 1/S).
//
// So this is not an approximation of the reference -- it is the same function
// with the trigonometry cancelled out, at roughly a tenth of the cost.
inline void saturateHsl(float k, int* r, int* g, int* b) {
  const int mx = *r > *g ? (*r > *b ? *r : *b) : (*g > *b ? *g : *b);
  const int mn = *r < *g ? (*r < *b ? *r : *b) : (*g < *b ? *g : *b);
  if (mx == mn) return;   // neutral: the reference skips these outright

  const float L   = (mx + mn) * 0.5f;
  const float d   = (float)(mx - mn);
  const float sum = (float)(mx + mn);
  // s = l > 0.5 ? d / (2 - max - min) : d / (max + min), in 0..255 units
  const float s = (L > 127.5f) ? d / fmaxf(510.0f - sum, 1e-6f) : d / fmaxf(sum, 1e-6f);
  const float f = fminf(k, s > 0.0f ? 1.0f / s : k);

  *r = csClamp8((int)(L + (*r - L) * f + 0.5f));
  *g = csClamp8((int)(L + (*g - L) * f + 0.5f));
  *b = csClamp8((int)(L + (*b - L) * f + 0.5f));
}

// applyPaperNormalization() in epdoptimize/src/dither/processing.ts.
void paperNormPixel(float strength, int* rp, int* gp, int* bp) {
  const int r = *rp, g = *gp, b = *bp;
  const float luma = csLuma709(r, g, b);
  const float sat  = csSaturation(r, g, b);

  // Red poster ink: push it further from the paper rather than neutralising it.
  if (sat >= 0.34f && r >= g + 24 && r >= b + 28) {
    const float boost = strength * kPaperPreserveRed;
    *rp = csClamp8((int)(r + (255 - r) * 0.08f * boost + 0.5f));
    *gp = csClamp8((int)(g * (1.0f - 0.08f * boost) + 0.5f));
    *bp = csClamp8((int)(b * (1.0f - 0.12f * boost) + 0.5f));
    return;
  }

  // Dark neutral ink: anchor it towards true black so text stays text.
  const float darkMask = csNormalize(112.0f - luma, 0.0f, 72.0f) *
                         csNormalize(0.42f - sat, 0.0f, 0.32f);
  if (darkMask > 0.0f) {
    const float amount = darkMask * kPaperBlackAnchor * strength;
    *rp = csClamp8((int)(r * (1.0f - 0.72f * amount) + 0.5f));
    *gp = csClamp8((int)(g * (1.0f - 0.72f * amount) + 0.5f));
    *bp = csClamp8((int)(b * (1.0f - 0.72f * amount) + 0.5f));
    return;
  }

  // Warm, bright, desaturated: that is aged paper. Neutralise it.
  const float warmBias = fminf((float)(r - b), (r + g) * 0.5f - b);
  const float paperMask = csNormalize(luma, kPaperMinLuma, 210.0f) *
                          csNormalize(245.0f - luma, 0.0f, 80.0f) *
                          csNormalize(kPaperSatThreshold - sat, 0.0f, kPaperSatThreshold) *
                          csNormalize(warmBias, kPaperWarmBias, 34.0f);
  if (paperMask <= 0.0f) return;

  const float amount = paperMask * strength;
  const float target = fminf(252.0f, luma + (kPaperWhite[0] - luma) * (0.72f + 0.2f * strength));
  const float nr = target + (kPaperWhite[0] - 248) * 0.4f;
  const float ng = target + (kPaperWhite[1] - 248) * 0.4f;
  const float nb = target + (kPaperWhite[2] - 248) * 0.4f;

  *rp = csClamp8((int)(r + (nr - r) * amount + 0.5f));
  *gp = csClamp8((int)(g + (ng - g) * amount + 0.5f));
  *bp = csClamp8((int)(b + (nb - b) * amount + 0.5f));
}

}  // namespace

// ---------------------------------------------------------------------------
//  Presets
// ---------------------------------------------------------------------------

void imgprocPreset(uint8_t preset, ProcSettings* out, uint8_t* colorMatching, uint8_t* edMatrix) {
  ProcSettings p;          // every field already at its neutral default
  uint8_t cm = CM_RGB;
  uint8_t ed = ED_FLOYD_STEINBERG;

  switch (preset) {
    case PROC_BALANCED:
      // "Compresses display luminance range for general photo conversion."
      p.toneMode = TONE_CONTRAST; p.contrast = 1.0f;
      p.drcMode = DRC_DISPLAY;    p.drcStrength = 1.0f;
      break;

    case PROC_DYNAMIC:
      // "S-curve tone mapping for brighter, punchier photographic output."
      p.saturation = 1.3f;
      p.toneMode = TONE_SCURVE;
      p.scStrength = 0.9f; p.scShadow = 0.0f; p.scHighlight = 1.5f; p.scMidpoint = 0.5f;
      p.drcMode = DRC_OFF;
      break;

    case PROC_VIVID:
      // "Boosts color and applies a gentler S-curve for illustrations."
      p.exposure = 1.1f; p.saturation = 1.6f;
      p.toneMode = TONE_SCURVE;
      p.scStrength = 0.7f; p.scShadow = 0.1f; p.scHighlight = 1.3f; p.scMidpoint = 0.5f;
      p.drcMode = DRC_OFF;
      break;

    case PROC_SOFT:
      // "Reduces contrast and uses Stucki diffusion for smoother tones."
      p.saturation = 1.1f;
      p.toneMode = TONE_CONTRAST; p.contrast = 0.9f;
      p.drcMode = DRC_DISPLAY;    p.drcStrength = 1.0f;
      ed = ED_STUCKI;
      break;

    case PROC_GRAYSCALE:
      // "Removes saturation and uses LAB matching for monochrome work."
      p.saturation = 0.0f;
      p.toneMode = TONE_SCURVE;
      p.scStrength = 0.8f; p.scShadow = 0.1f; p.scHighlight = 1.4f; p.scMidpoint = 0.5f;
      p.drcMode = DRC_DISPLAY; p.drcStrength = 1.0f;
      cm = CM_LAB;
      break;

    case PROC_RESTORE:
      // "Expands faded scans and paintings before mapping them to the range."
      p.exposure = 1.08f; p.saturation = 0.9f;
      p.toneMode = TONE_SCURVE;
      p.scStrength = 1.0f; p.scShadow = 0.25f; p.scHighlight = 0.75f; p.scMidpoint = 0.46f;
      p.drcMode = DRC_AUTO; p.drcStrength = 0.9f;
      p.drcLowPct = 0.02f;  p.drcHighPct = 0.98f;
      p.drcPreserveWhite = true;   // epdoptimize enables this for active range fitting
      cm = CM_LAB;
      break;

    case PROC_POSTERSCAN:
      // "Neutralises warm paper, anchors black ink, preserves poster colours."
      p.paperMode = PAPER_WARM; p.paperStrength = 0.95f;
      p.exposure = 1.04f; p.saturation = 1.05f;
      p.toneMode = TONE_SCURVE;
      p.scStrength = 0.92f; p.scShadow = 0.08f; p.scHighlight = 0.55f; p.scMidpoint = 0.44f;
      p.drcMode = DRC_AUTO; p.drcStrength = 1.0f;
      p.drcLowPct = 0.015f; p.drcHighPct = 0.985f;
      p.drcPreserveWhite = true;
      break;

    case PROC_CUSTOM:
    default:
      break;   // everything neutral: this is the firmware default
  }

  const uint8_t keep = out->preset;
  *out = p;
  out->preset = keep;
  if (colorMatching) *colorMatching = cm;
  if (edMatrix)      *edMatrix = ed;
}

const char* procPresetName(uint8_t p) {
  switch (p) {
    case PROC_CUSTOM:     return "custom";
    case PROC_BALANCED:   return "balanced";
    case PROC_DYNAMIC:    return "dynamic";
    case PROC_VIVID:      return "vivid";
    case PROC_SOFT:       return "soft";
    case PROC_GRAYSCALE:  return "grayscale";
    case PROC_RESTORE:    return "restore";
    case PROC_POSTERSCAN: return "posterScan";
  }
  return "custom";
}

const char* toneModeName(uint8_t m) {
  switch (m) {
    case TONE_OFF:      return "off";
    case TONE_CONTRAST: return "contrast";
    case TONE_SCURVE:   return "scurve";
  }
  return "off";
}

const char* drcModeName(uint8_t m) {
  switch (m) {
    case DRC_OFF:     return "off";
    case DRC_DISPLAY: return "display";
    case DRC_AUTO:    return "auto";
  }
  return "off";
}

const char* levelModeName(uint8_t m) {
  switch (m) {
    case LVL_OFF:         return "off";
    case LVL_PER_CHANNEL: return "perChannel";
    case LVL_LUMA:        return "luma";
  }
  return "off";
}

const char* paperModeName(uint8_t m) {
  switch (m) {
    case PAPER_OFF:  return "off";
    case PAPER_WARM: return "warmPaper";
  }
  return "off";
}

// ---------------------------------------------------------------------------
//  Source pass: gamma, paper normalisation, clarity
// ---------------------------------------------------------------------------

bool imgprocSourcePassNeeded(const ProcSettings& ps, float gamma) {
  const bool doGamma = !(gamma > 0.999f && gamma < 1.001f);
  const bool doPaper = ps.paperMode == PAPER_WARM && ps.paperStrength > 0.0f;
  const bool doClar  = ps.clarityAmount < -0.001f || ps.clarityAmount > 0.001f;
  return doGamma || doPaper || doClar;
}

bool imgprocSourcePass(uint16_t* px, int w, int h, const ProcSettings& ps, float gamma,
                       float outScale) {
  if (!px || w < 1 || h < 1) return true;
  csInit();

  const bool doGamma = !(gamma > 0.999f && gamma < 1.001f);
  const bool doPaper = ps.paperMode == PAPER_WARM && ps.paperStrength > 0.0f;
  const bool doClar  = ps.clarityAmount < -0.001f || ps.clarityAmount > 0.001f;
  if (!doGamma && !doPaper && !doClar) return true;

  uint8_t gLut[256];
  for (int i = 0; i < 256; ++i) {
    gLut[i] = doGamma
                  ? (uint8_t)csClamp8((int)(powf(i / 255.0f, 1.0f / gamma) * 255.0f + 0.5f))
                  : (uint8_t)i;
  }
  const float paperStr = csClampf(ps.paperStrength, 0.0f, 1.0f);

  // --- the cheap path: both remaining stages are pointwise ------------------
  if (!doClar) {
    for (int y = 0; y < h; ++y) {
      uint16_t* row = px + (size_t)y * w;
      for (int x = 0; x < w; ++x) {
        int r, g, b;
        unpack565(row[x], &r, &g, &b);
        r = gLut[r]; g = gLut[g]; b = gLut[b];
        if (doPaper) paperNormPixel(paperStr, &r, &g, &b);
        row[x] = pack565(r, g, b);
      }
    }
    return true;
  }

  // --- clarity: separable box blur over a sliding window -------------------
  //
  // epdoptimize's applyClarity() keeps two full-image RGBA copies
  // (getClarityScratch). At source size that would be ~12 MB here. Instead we
  // keep two rings of 2*radius+1 rows: `hb` holds the horizontal-blur result
  // (read 2*radius+1 times per output row, so it goes in internal RAM when it
  // fits) and `orig` holds the post-gamma/paper source (read once per output
  // row, so PSRAM is fine). About 78 kB at radius 4 and width 1600.
  //
  // The radius is scaled by the render scale factor so that the effective
  // radius in OUTPUT pixels matches what the references produce on a canvas
  // that has already been resized. Without this, the same slider position
  // would mean different things for a 1440 px preview and a 6000 px original.
  const int radius = (int)csClampf(roundf(ps.clarityRadius * (outScale > 0.0f ? 1.0f / outScale : 1.0f)),
                                   1.0f, 4.0f);
  const int kernel = radius * 2 + 1;
  const size_t rowBytes = (size_t)w * 3;

  uint8_t* hb   = (uint8_t*)malloc(rowBytes * kernel);
  if (!hb) hb   = (uint8_t*)ps_malloc(rowBytes * kernel);
  uint8_t* orig = (uint8_t*)ps_malloc(rowBytes * kernel);
  if (!hb || !orig) {
    free(hb); free(orig);
    // Still apply the pointwise stages -- losing clarity is not losing the frame.
    if (doGamma || doPaper) {
      for (int y = 0; y < h; ++y) {
        uint16_t* row = px + (size_t)y * w;
        for (int x = 0; x < w; ++x) {
          int r, g, b;
          unpack565(row[x], &r, &g, &b);
          r = gLut[r]; g = gLut[g]; b = gLut[b];
          if (doPaper) paperNormPixel(paperStr, &r, &g, &b);
          row[x] = pack565(r, g, b);
        }
      }
    }
    return false;
  }

  // The midtone weight is computed exactly, with one powf per pixel. A
  // 256-entry table indexed by rounded luma was tried and removed: the weight
  // is pow(1-|2l-1|, midtone), whose derivative is unbounded at l = 0 and
  // l = 1 for midtone < 1, so at midtone 0.5 the table was off by 0.06 at the
  // very darkest and brightest pixels -- up to 32 output units on a hard edge.
  // Interpolating or widening the table does not fix a singularity. The
  // reference evaluates this once per pixel too (outside its channel loop), so
  // there was never a factor of three to win here, only accuracy to lose.
  const float midtone = fmaxf(0.1f, ps.clarityMidtone);
  const float effAmount = csClampf(ps.clarityAmount, -1.0f, 1.0f) * 2.0f;

  // Logical row j covers source row clamp(j - radius, 0, h-1); output row y
  // needs j in [y, y + 2*radius] and its own data at j = y + radius.
  auto loadLogical = [&](int j) {
    const int sy = (int)csClampf((float)(j - radius), 0.0f, (float)(h - 1));
    const int slot = j % kernel;
    uint8_t* o = orig + (size_t)slot * rowBytes;
    const uint16_t* row = px + (size_t)sy * w;

    for (int x = 0; x < w; ++x) {
      int r, g, b;
      unpack565(row[x], &r, &g, &b);
      r = gLut[r]; g = gLut[g]; b = gLut[b];
      if (doPaper) paperNormPixel(paperStr, &r, &g, &b);
      o[x * 3] = (uint8_t)r; o[x * 3 + 1] = (uint8_t)g; o[x * 3 + 2] = (uint8_t)b;
    }

    // Horizontal box blur with a running sum and clamp-to-edge, exactly as the
    // reference's inner loop does it.
    uint8_t* hbr = hb + (size_t)slot * rowBytes;
    int sum[3] = {0, 0, 0};
    for (int k = -radius; k <= radius; ++k) {
      const int xi = (int)csClampf((float)k, 0.0f, (float)(w - 1));
      for (int c = 0; c < 3; ++c) sum[c] += o[xi * 3 + c];
    }
    for (int x = 0; x < w; ++x) {
      for (int c = 0; c < 3; ++c) hbr[x * 3 + c] = (uint8_t)(sum[c] / kernel);
      const int rx = (int)csClampf((float)(x - radius), 0.0f, (float)(w - 1));
      const int ax = (int)csClampf((float)(x + radius + 1), 0.0f, (float)(w - 1));
      for (int c = 0; c < 3; ++c) sum[c] += o[ax * 3 + c] - o[rx * 3 + c];
    }
  };

  for (int j = 0; j <= 2 * radius; ++j) loadLogical(j);

  for (int y = 0; y < h; ++y) {
    const uint8_t* src = orig + (size_t)((y + radius) % kernel) * rowBytes;
    uint16_t* dst = px + (size_t)y * w;

    for (int x = 0; x < w; ++x) {
      int blur[3] = {0, 0, 0};
      for (int j = y; j <= y + 2 * radius; ++j) {
        const uint8_t* hbr = hb + (size_t)(j % kernel) * rowBytes;
        for (int c = 0; c < 3; ++c) blur[c] += hbr[x * 3 + c];
      }

      const int r0 = src[x * 3], g0 = src[x * 3 + 1], b0 = src[x * 3 + 2];
      const float l = csLuma709(r0, g0, b0) / 255.0f;
      const float wgt = powf(csClampf(1.0f - fabsf(2.0f * l - 1.0f), 0.0f, 1.0f), midtone);

      const int r = csClamp8((int)(r0 + effAmount * (r0 - blur[0] / (float)kernel) * wgt + 0.5f));
      const int g = csClamp8((int)(g0 + effAmount * (g0 - blur[1] / (float)kernel) * wgt + 0.5f));
      const int b = csClamp8((int)(b0 + effAmount * (b0 - blur[2] / (float)kernel) * wgt + 0.5f));
      dst[x] = pack565(r, g, b);
    }

    // Output row y needs logical rows [y, y+2*radius]; the last one ever needed
    // is h-1+2*radius, so only advance while another output row follows.
    if (y + 1 < h) loadLogical(y + 1 + 2 * radius);
  }

  free(hb);
  free(orig);
  return true;
}

// ---------------------------------------------------------------------------
//  Analysis
// ---------------------------------------------------------------------------

void imgprocAnalyze(const uint16_t* px, int w, int h, ImgProcRect rect, const E6Palette& pal,
                    const ProcSettings& ps, ImgProcPlan* out) {
  csInit();
  *out = ImgProcPlan();

  // --- pointwise tone tables (independent of the image) --------------------
  const float expo = csClampf(ps.exposure, 0.05f, 4.0f);
  out->doExposure = !(expo > 0.999f && expo < 1.001f);
  for (int i = 0; i < 256; ++i) out->expoLut[i] = (uint8_t)csClamp8((int)(i * expo + 0.5f));

  out->satMul = csClampf(ps.saturation, 0.0f, 4.0f);
  out->doSat  = !(out->satMul > 0.999f && out->satMul < 1.001f);

  if (ps.toneMode == TONE_CONTRAST) {
    const float c = csClampf(ps.contrast, 0.05f, 4.0f);
    out->doTone = !(c > 0.999f && c < 1.001f);
    for (int i = 0; i < 256; ++i)
      out->toneLut[i] = (uint8_t)csClamp8((int)((i - 128) * c + 128.0f + 0.5f));
  } else if (ps.toneMode == TONE_SCURVE && ps.scStrength > 0.0f) {
    out->doTone = true;
    buildScurve(csClampf(ps.scStrength, 0.0f, 1.0f), csClampf(ps.scShadow, 0.0f, 1.0f),
                csClampf(ps.scHighlight, 0.0f, 5.0f), ps.scMidpoint, out->toneLut);
  } else {
    for (int i = 0; i < 256; ++i) out->toneLut[i] = (uint8_t)i;
  }

  // --- DRC endpoints -------------------------------------------------------
  const bool drcOn = ps.drcMode != DRC_OFF && ps.drcStrength > 0.0f;
  out->drcMode     = drcOn ? ps.drcMode : DRC_OFF;
  out->drcAccurate = ps.drcQuality == DRCQ_ACCURATE;
  out->drcStrength = csClampf(ps.drcStrength, 0.0f, 1.0f);

  const int slotBlack = paletteDarkest(pal);
  const int slotWhite = paletteLightest(pal);

  if (drcOn) {
    // getPaletteEndpoints() in epdoptimize / cdr_init() in esp32-photoframe:
    // the compression target is the palette's own darkest and lightest entries.
    const float yB = csLinearY(pal.rgb[slotBlack][0], pal.rgb[slotBlack][1], pal.rgb[slotBlack][2]);
    const float yW = csLinearY(pal.rgb[slotWhite][0], pal.rgb[slotWhite][1], pal.rgb[slotWhite][2]);
    if (out->drcAccurate) {
      out->drcTargetLo    = csLstarFromY(yB);
      out->drcTargetRange = csLstarFromY(yW) - out->drcTargetLo;
      out->drcSrcLo = 0.0f;
      out->drcSrcRange = 100.0f;
    } else {
      out->drcTargetLo    = yB;
      out->drcTargetRange = yW - yB;
      out->drcSrcLo = 0.0f;
      out->drcSrcRange = 1.0f;
    }
    if (out->drcTargetRange <= 0.0f) out->drcMode = DRC_OFF;
  }

  // --- level endpoints ----------------------------------------------------
  //
  // epdoptimize's levelCompression takes an explicit black/white and is the
  // identity without one, so the only configuration in which it does anything
  // is "remap into the palette's range" -- which is what DRC_DISPLAY already
  // does, in luminance rather than per channel. It is kept because it is in
  // the reference's option set, defaults off, and is labelled as the legacy
  // remap in the UI. esp32-photoframe's cdr comment records that they tried a
  // per-channel remap and reverted it: "it compresses chroma along with
  // lightness and visibly washes out midtones."
  out->levelMode = ps.levelMode;
  {
    const uint8_t* blk = pal.rgb[slotBlack];
    const uint8_t* wht = pal.rgb[slotWhite];
    for (int c = 0; c < 3; ++c) {
      const int d = wht[c] - blk[c];
      for (int i = 0; i < 256; ++i)
        out->lvlLut[c][i] = (uint8_t)csClamp8(blk[c] + (i * d) / 255);
      if (d <= 0 && ps.levelMode == LVL_PER_CHANNEL) out->levelMode = LVL_OFF;
    }
    out->lvlBlackY = csLuma709(blk[0], blk[1], blk[2]);
    out->lvlRangeY = csLuma709(wht[0], wht[1], wht[2]) - out->lvlBlackY;
    if (out->lvlRangeY <= 0.0f && ps.levelMode == LVL_LUMA) out->levelMode = LVL_OFF;
  }

  // --- white preservation -------------------------------------------------
  out->presWhite  = ps.drcPreserveWhite && out->drcMode != DRC_OFF;
  out->presMaxSat = 0.18f;
  for (int c = 0; c < 3; ++c) out->presRgb[c] = pal.rgb[slotWhite][c];
  out->presTargetLuma = csLuma709(out->presRgb[0], out->presRgb[1], out->presRgb[2]);

  const bool needAuto  = out->drcMode == DRC_AUTO;
  const bool needLevel = ps.levelMode != LVL_OFF && ps.levelAuto;
  const bool needScan  = needAuto || needLevel || out->presWhite;

  out->any = out->doExposure || out->doSat || out->doTone || out->drcMode != DRC_OFF ||
             out->levelMode != LVL_OFF;

  if (!needScan || !px) {
    if (out->presWhite) out->presWhite = false;   // cannot honour it without the scan
    return;
  }

  // --- one subsampled read-only pass --------------------------------------
  //
  // Every second pixel in each axis, and only inside `rect`, which is the part
  // of the source that will actually reach the panel: a cover-mode crop throws
  // away up to half the image, and letting those pixels into the percentiles
  // would set the range from tones nobody will see. Percentiles are robust to
  // 4:1 subsampling, and this saves three quarters of the work.
  memset(s_histQ, 0, sizeof(s_histQ));
  memset(s_histWhite, 0, sizeof(s_histWhite));
  uint32_t nQ = 0, nWhite = 0, nOut = 0, nTot = 0;

  const int x0 = (int)csClampf((float)rect.x0, 0.0f, (float)(w - 1));
  const int x1 = (int)csClampf((float)rect.x1, 0.0f, (float)(w - 1));
  const int y0 = (int)csClampf((float)rect.y0, 0.0f, (float)(h - 1));
  const int y1 = (int)csClampf((float)rect.y1, 0.0f, (float)(h - 1));

  for (int y = y0; y <= y1; y += 2) {
    const uint16_t* row = px + (size_t)y * w;
    for (int x = x0; x <= x1; x += 2) {
      int r, g, b;
      unpack565(row[x], &r, &g, &b);

      // White preservation is keyed off the SOURCE pixel, matching
      // getWhitePreservationPlan(), which runs before the adjustments.
      if (out->presWhite) {
        if (csSaturation(r, g, b) <= out->presMaxSat) {
          s_histWhite[csClamp8((int)(csLuma709(r, g, b) + 0.5f))]++;
          nWhite++;
        }
      }

      // Everything below sees the image as DRC will: exposure, saturation and
      // tone already applied.
      if (out->doExposure) { r = out->expoLut[r]; g = out->expoLut[g]; b = out->expoLut[b]; }
      if (out->doSat) saturateHsl(out->satMul, &r, &g, &b);
      if (out->doTone) { r = out->toneLut[r]; g = out->toneLut[g]; b = out->toneLut[b]; }

      if (needAuto) {
        const float Y = csLinearY(r, g, b);
        const float q = out->drcAccurate ? csLstarFromY(Y) * 0.01f : Y;
        s_histQ[(int)csClampf(q * (kHistBins - 1) + 0.5f, 0.0f, (float)(kHistBins - 1))]++;
        nQ++;
      }

      if (needLevel) {
        nTot++;
        if (ps.levelMode == LVL_PER_CHANNEL) {
          const uint8_t* blk = pal.rgb[slotBlack];
          const uint8_t* wht = pal.rgb[slotWhite];
          if (r < blk[0] || r > wht[0] || g < blk[1] || g > wht[1] || b < blk[2] || b > wht[2])
            nOut++;
        } else {
          const float y709 = csLuma709(r, g, b);
          if (y709 < out->lvlBlackY || y709 > out->lvlBlackY + out->lvlRangeY) nOut++;
        }
      }
    }
  }

  if (needAuto) {
    const float lo = percentile(s_histQ, nQ, csClampf(ps.drcLowPct, 0.0f, 1.0f));
    const float hi = percentile(s_histQ, nQ, csClampf(ps.drcHighPct, 0.0f, 1.0f));
    const float span = out->drcAccurate ? 100.0f : 1.0f;
    out->drcSrcLo    = lo * span;
    out->drcSrcRange = (hi - lo) * span;
    // A flat image gives hi == lo; compressing it would divide by zero and is
    // meaningless anyway.
    if (out->drcSrcRange <= 1e-4f) out->drcMode = DRC_OFF;
  }

  if (out->presWhite) {
    if (nWhite == 0) {
      out->presWhite = false;
    } else {
      out->presLuma = percentile(s_histWhite, nWhite, 0.99f) * 255.0f;
      // whitePreserveMinLuma: below this the image has no background white to
      // protect and snapping would invent one.
      if (out->presLuma < 150.0f) out->presWhite = false;
    }
  }

  if (needLevel && !(nTot > 0 && (float)nOut / (float)nTot >= 0.01f)) out->levelMode = LVL_OFF;

  out->any = out->doExposure || out->doSat || out->doTone || out->drcMode != DRC_OFF ||
             out->levelMode != LVL_OFF || out->presWhite;
}

// ---------------------------------------------------------------------------
//  Row pass
// ---------------------------------------------------------------------------

void imgprocRow(const ImgProcPlan& plan, uint8_t* rgb888, int n) {
  if (!plan.any) return;

  for (int i = 0; i < n; ++i) {
    uint8_t* p = rgb888 + i * 3;
    int r = p[0], g = p[1], b = p[2];

    const int sr = r, sg = g, sb = b;   // kept for white preservation

    if (plan.doExposure) { r = plan.expoLut[r]; g = plan.expoLut[g]; b = plan.expoLut[b]; }
    if (plan.doSat) saturateHsl(plan.satMul, &r, &g, &b);
    if (plan.doTone) { r = plan.toneLut[r]; g = plan.toneLut[g]; b = plan.toneLut[b]; }

    // --- dynamic range compression ---------------------------------------
    //
    // Luminance is remapped into the palette's range and all three channels
    // are then scaled by the same factor. That is cdr_apply_row() in
    // esp32-photoframe/main/image_processor.c, whose comment explains the
    // choice: "the known deviation from epaper-image-convert, which compresses
    // CIELAB lightness and preserves chroma: scaling luminance proportionally
    // keeps chromaticity while avoiding a per-pixel Lab round-trip on device."
    //
    // On top of that we take two refinements from epdoptimize: DRC_AUTO uses
    // measured source percentiles instead of assuming the source fills 0..1,
    // and the chroma-protection mask holds saturated pixels back so they are
    // not flattened along with the neutrals.
    if (plan.drcMode != DRC_OFF) {
      const float lr = csSrgbToLinear(r), lg = csSrgbToLinear(g), lb = csSrgbToLinear(b);
      const float Y = 0.2126729f * lr + 0.7151522f * lg + 0.0721750f * lb;

      const float q  = plan.drcAccurate ? csLstarFromY(Y) : Y;
      const float nn = csClampf((q - plan.drcSrcLo) / plan.drcSrcRange, 0.0f, 1.0f);
      const float q2 = plan.drcTargetLo + nn * plan.drcTargetRange;
      const float Yt = plan.drcAccurate ? yFromLstar(q2) : q2;

      const float cp  = csSmoothstep(0.18f, 0.68f, csSaturation(r, g, b)) * 0.85f;
      const float eff = plan.drcStrength * (1.0f - cp);
      const float Yn  = Y + (Yt - Y) * eff;

      if (Y > 1e-6f) {
        float scale = Yn / Y;
        const float mx = fmaxf(lr, fmaxf(lg, lb));
        if (mx > 0.0f) scale = fminf(scale, 1.0f / mx);
        r = csLinearToSrgb(lr * scale);
        g = csLinearToSrgb(lg * scale);
        b = csLinearToSrgb(lb * scale);
      } else {
        // Near-black: there is no ratio to scale, so sit the pixel on the
        // display's black level instead of leaving it at true zero.
        r = g = b = csLinearToSrgb(Yn);
      }
    }

    // --- level compression ------------------------------------------------
    if (plan.levelMode == LVL_PER_CHANNEL) {
      r = plan.lvlLut[0][r]; g = plan.lvlLut[1][g]; b = plan.lvlLut[2][b];
    } else if (plan.levelMode == LVL_LUMA) {
      const float y = csLuma709(r, g, b);
      if (y > 0.0f) {
        const float yn = plan.lvlBlackY + (y * plan.lvlRangeY) / 255.0f;
        float ratio = yn / y;
        const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
        if (mx > 0) ratio = fminf(ratio, 255.0f / mx);
        r = csClamp8((int)(r * ratio + 0.5f));
        g = csClamp8((int)(g * ratio + 0.5f));
        b = csClamp8((int)(b * ratio + 0.5f));
      } else {
        r = g = b = csClamp8((int)(plan.lvlBlackY + 0.5f));
      }
    }

    // --- white preservation ----------------------------------------------
    //
    // applyWhitePreservation(): a pixel that was background white in the
    // source should not come out darker than the palette's white, or a scan's
    // paper picks up a grey cast from the range fitting.
    if (plan.presWhite && csSaturation(sr, sg, sb) <= plan.presMaxSat &&
        csLuma709(sr, sg, sb) + 1e-4f >= plan.presLuma &&
        csLuma709(r, g, b) < plan.presTargetLuma) {
      r = plan.presRgb[0]; g = plan.presRgb[1]; b = plan.presRgb[2];
    }

    p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b;
  }
}
