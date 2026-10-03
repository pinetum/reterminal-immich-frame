#include "colorspace.h"

float   g_csToLinear[256];
uint8_t g_csFromLinear[CS_FROM_LINEAR_BINS];
float   g_csPivot[CS_PIVOT_BINS + 1];

static bool s_ready = false;

void csInit() {
  if (s_ready) return;

  for (int i = 0; i < 256; ++i) {
    const float s = i / 255.0f;
    g_csToLinear[i] = s > 0.04045f ? powf((s + 0.055f) / 1.055f, 2.4f) : s / 12.92f;
  }

  for (int i = 0; i < CS_FROM_LINEAR_BINS; ++i) {
    const float lin = (float)i / (CS_FROM_LINEAR_BINS - 1);
    const float s = lin > 0.0031308f ? 1.055f * powf(lin, 1.0f / 2.4f) - 0.055f : 12.92f * lin;
    g_csFromLinear[i] = (uint8_t)csClamp8((int)(s * 255.0f + 0.5f));
  }

  for (int i = 0; i <= CS_PIVOT_BINS; ++i) {
    const float t = (float)i / CS_PIVOT_BINS;
    g_csPivot[i] = t > 0.008856f ? cbrtf(t) : 7.787f * t + 16.0f / 116.0f;
  }

  s_ready = true;
}

void csRgbToLab(int r, int g, int b, float lab[3]) {
  const float rl = g_csToLinear[r], gl = g_csToLinear[g], bl = g_csToLinear[b];

  // sRGB -> XYZ (D65), then normalised by the white point. epdoptimize scales
  // XYZ by 100 and divides by 95.047/100/108.883; folding the 100 away leaves
  // the same ratios with one multiply less per channel.
  const float x = (rl * 0.4124564f + gl * 0.3575761f + bl * 0.1804375f) / 0.95047f;
  const float y = (rl * 0.2126729f + gl * 0.7151522f + bl * 0.0721750f);
  const float z = (rl * 0.0193339f + gl * 0.1191920f + bl * 0.9503041f) / 1.08883f;

  const float fx = csPivot(x), fy = csPivot(y), fz = csPivot(z);

  lab[0] = 116.0f * fy - 16.0f;
  lab[1] = 500.0f * (fx - fy);
  lab[2] = 200.0f * (fy - fz);
}

float csHue(int r, int g, int b) {
  const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
  const int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  const int d = mx - mn;
  if (d == 0) return 0.0f;

  float h;
  if (mx == r)      h = 60.0f * fmodf((float)(g - b) / d, 6.0f);
  else if (mx == g) h = 60.0f * ((float)(b - r) / d + 2.0f);
  else              h = 60.0f * ((float)(r - g) / d + 4.0f);

  return h < 0.0f ? h + 360.0f : h;
}
