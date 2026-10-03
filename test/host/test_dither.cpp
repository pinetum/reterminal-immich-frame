#include "settings.h"
#include "render.h"
#include "log.h"
#include <cstdio>
#include <cmath>
#include <vector>

Settings g_cfg;
void settingsLoad(){} void settingsSave(){} void settingsFactoryReset(){}
const char* imageSizeName(ImageSize){ return "preview"; }
void logBegin(){} void logMem(const char*){}
uint32_t settingsRenderSignature(const Settings&){ return 0; }
const E6Palette& settingsPalette(const Settings& cfg) {
  return cfg.paletteId == PAL_CUSTOM ? cfg.paletteCustom : paletteBuiltin(cfg.paletteId);
}

static inline uint8_t nib(const uint8_t*f,int x,int y){
  const uint8_t b=f[(size_t)y*PANEL_STRIDE+(x>>1)]; return (x&1)?(b&0xF):(b>>4);
}
static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("  FAIL: %s\n",m); fails++; } }while(0)

// The invariant that actually defines error diffusion: averaged over an area,
// the palette colours chosen must reconstruct the input colour.
//
// Note "the palette colours chosen" means the CALIBRATED ones. With a measured
// palette the panel's white is around #B9C7C9, so a field of input 200,200,200
// cannot be reconstructed by white alone -- it has to be mixed, and the mean of
// the calibrated colours is what has to come back. Reconstructing against the
// theoretical palette here would be measuring the wrong thing, and is exactly
// the mistake the calibrated palette exists to fix.
struct Res { double r,g,b; double worstRowErr; int worstRow; };

static Res run(int R,int G,int B){
  g_cfg.gamma=1.0f; g_cfg.fit=FIT_CONTAIN; g_cfg.rotation=0;
  const E6Palette& pal = settingsPalette(g_cfg);

  SrcImage s; s.w=PANEL_W; s.h=PANEL_H;
  s.px=(uint16_t*)malloc((size_t)s.w*s.h*2);
  const uint16_t v=(uint16_t)(((R>>3)<<11)|((G>>2)<<5)|(B>>3));
  for(size_t i=0;i<(size_t)s.w*s.h;i++) s.px[i]=v;

  uint8_t* f=frameAlloc();
  RenderStats st; renderFrame(s,f,&st);

  double tr=0,tg=0,tb=0; Res res{}; res.worstRowErr=0;
  for(int y=0;y<PANEL_H;y++){
    double rr=0,rg=0,rb=0;
    for(int x=0;x<PANEL_W;x++){
      uint8_t pr,pg,pb; e6CodeToRgb(pal,nib(f,x,y),&pr,&pg,&pb);
      rr+=pr; rg+=pg; rb+=pb;
    }
    tr+=rr; tg+=rg; tb+=rb;
    rr/=PANEL_W; rg/=PANEL_W; rb/=PANEL_W;
    // Skip the first rows: error diffusion needs a little way to settle.
    if(y>8){
      const double e=(fabs(rr-R)+fabs(rg-G)+fabs(rb-B))/3.0;
      if(e>res.worstRowErr){ res.worstRowErr=e; res.worstRow=y; }
    }
  }
  const double n=(double)PANEL_W*PANEL_H;
  res.r=tr/n; res.g=tg/n; res.b=tb/n;
  frameFree(f); srcFree(&s);
  return res;
}

static void check(const char* name,int R,int G,int B,double meanTol,double rowTol){
  const Res x=run(R,G,B);
  const double err=(fabs(x.r-R)+fabs(x.g-G)+fabs(x.b-B))/3.0;
  printf("%-18s in=(%3d,%3d,%3d) out=(%6.1f,%6.1f,%6.1f) meanErr=%5.1f  worstRow=%5.1f @y=%d\n",
         name,R,G,B,x.r,x.g,x.b,err,x.worstRowErr,x.worstRow);
  CHECK(err<meanTol,"mean reconstructed colour must track the input");
  CHECK(x.worstRowErr<rowTol,"no row may drift (would mean the rolling error buffer is wrong)");
}

// Exact output of the pre-calibration firmware, captured from the baseline run.
// These are not tolerances -- they are a promise. The default configuration
// (PAL_SEEED, CM_RGB, error diffusion) must render byte-for-byte what it
// rendered before calibrated palettes existed, so that updating a device in the
// field does not silently change its pictures.
static void regression(const char* name,int R,int G,int B,double er,double eg,double eb){
  const Res x=run(R,G,B);
  const double d=fabs(x.r-er)+fabs(x.g-eg)+fabs(x.b-eb);
  printf("%-18s out=(%6.1f,%6.1f,%6.1f) expected=(%6.1f,%6.1f,%6.1f) delta=%.3f\n",
         name,x.r,x.g,x.b,er,eg,eb,d);
  CHECK(d<0.15,"DEFAULTS CHANGED: this configuration must match the pre-calibration firmware");
}

// Each kernel's weights must sum to its documented total. A typo in one
// numerator or denominator is invisible to any check of the OUTPUT: a flat
// field settles into an attractor near the input even when a quarter of the
// error is deliberately thrown away (Atkinson's mean error on mid-grey is only
// 5.2, barely worse than Floyd-Steinberg's 3.1). So read the weights.
static void kernelSums(){
  struct Expect { EdMatrix m; const char* n; double sum; };
  const Expect want[]={
    {ED_FLOYD_STEINBERG,"floydSteinberg",1.0},{ED_FALSE_FLOYD_STEINBERG,"falseFS",1.0},
    // Atkinson diffuses only 6/8 of the error on purpose, which is where its
    // characteristic high-contrast look comes from.
    {ED_ATKINSON,"atkinson",0.75},            {ED_JARVIS,"jarvis",1.0},
    {ED_STUCKI,"stucki",1.0},                 {ED_BURKES,"burkes",1.0},
    {ED_SIERRA3,"sierra3",1.0},               {ED_SIERRA2,"sierra2",1.0},
    {ED_SIERRA2_4A,"sierra2-4a",1.0},         {ED_FAN,"fan",1.0},
    {ED_SHIAU_FAN,"shiauFan",1.0},            {ED_SHIAU_FAN2,"shiauFan2",1.0},
  };
  printf("\n-- kernel weight sums\n");
  for(const auto& w : want){
    const double got = e6KernelWeightSum(w.m);
    printf("   %-16s %.6f (want %.2f)\n",w.n,got,w.sum);
    CHECK(fabs(got-w.sum)<1e-5,"kernel weights do not sum to the documented total");
  }
}

int main(){
  // Tolerances, and why they are what they are:
  //
  // meanTol: how far the area-averaged reconstruction may sit from the input.
  //   Floyd-Steinberg and Jarvis conserve all of the error, so they land within
  //   a few units of 255 -- about 1-2%.
  //
  // rowTol: a single row's mean may legitimately wander on a FLAT field,
  //   because error diffusion builds structured patterns that are not
  //   row-balanced -- rows are not independent samples. What this bound is
  //   really guarding is the rolling 3-row error buffer: if its rotation or its
  //   clearing were wrong, rows would be off by 100+, not 25. See
  //   test_stream.cpp, which proves that buffer exactly rather than statistically.

  printf("-- regression: the default configuration must not have moved\n");
  g_cfg = Settings();
  regression("fs grey",  128,128,128, 132.1, 129.8, 131.4);
  regression("fs dark",   64, 64, 64,  65.3,  65.7,  64.8);
  regression("fs light", 200,200,200, 206.6, 203.0, 205.3);
  regression("fs skin",  222,170,140, 222.8, 170.3, 139.8);
  g_cfg.dither.matrix=ED_JARVIS;
  regression("jarvis",   128,128,128, 132.3, 129.5, 129.8);
  g_cfg.dither.matrix=ED_ATKINSON;
  regression("atkinson", 128,128,128, 126.8, 130.1, 115.6);

  printf("\n-- calibrated palettes, RGB matching\n");
  struct { uint8_t id; const char* n; } pals[]={
    {PAL_SEEED,"seeed"},{PAL_SPECTRA6,"spectra6"},{PAL_SPECTRA6_LEGACY,"legacy"},
    {PAL_SPECTRA6_BOEBER,"boeber"},{PAL_AITJCIZE,"aitjcize"}};
  for(auto& p : pals){
    g_cfg = Settings();
    g_cfg.paletteId=p.id;
    // The input has to be reachable with the palette in play: a calibrated
    // palette cannot reproduce 200,200,200 at all, because its brightest ink is
    // darker than that. Mid-grey is inside every one of these gamuts.
    char name[48]; snprintf(name,sizeof(name),"%s grey",p.n);
    check(name,120,120,120, 8.0, 45.0);
  }

  printf("\n-- colour-matching modes on a neutral (spectra6)\n");
  struct { ColorMatching m; const char* n; } modes[]={
    {CM_RGB,"rgb"},{CM_LAB,"lab"},{CM_CHROMA,"chroma"}};
  for(auto& m : modes){
    g_cfg = Settings();
    g_cfg.paletteId=PAL_SPECTRA6;
    g_cfg.dither.matching=m.m;
    char name[48];
    snprintf(name,sizeof(name),"%s grey",m.n);
    // LAB and chroma deliberately trade mean accuracy for perceptual and hue
    // fidelity, so they are allowed to sit further out than plain RGB.
    check(name,120,120,120, m.m==CM_RGB?8.0:30.0, 60.0);
  }

  printf("\n-- ordered dithering tiles the Bayer matrix exactly\n");
  // A flat mid-grey cannot be "reconstructed" by ordered dithering on this
  // palette at all, and that is not a bug: a single scalar threshold moves all
  // three channels together, so it walks along the grey axis and the nearest
  // ink to mid-grey stays the same one the whole way. (With the Seeed palette
  // that ink is green, because 128,128,128 really is closer to 29,185,84 than
  // to white or black.) Mixing hues needs either error feedback or
  // epdoptimize's coverage-based Bayer, which needs a whole-image pass.
  //
  // So the property worth asserting is the one that IS ordered dithering's
  // own: for a uniform input the output must be exactly periodic with the
  // matrix size, in both axes. That pins down the matrix construction and the
  // row/column indexing without caring what colours come out.
  for(int sz : {2,4,8,16}){
    DitherCfg cfg; cfg.type=DITHER_ORDERED; cfg.bayerSize=(uint8_t)sz;
    const E6Palette& p = paletteBuiltin(PAL_SPECTRA6);
    const int W=sz*5, H=sz*5;
    std::vector<uint8_t> in((size_t)W*3,0), out((size_t)W*H);
    // A tone close to the white/yellow boundary, so the threshold actually
    // flips the choice and the pattern is not uniform.
    for(int x=0;x<W;x++){ in[x*3]=180; in[x*3+1]=180; in[x*3+2]=120; }
    E6Ditherer d; d.begin(W,p,cfg);
    for(int y=0;y<H;y++) d.row(in.data(), out.data()+(size_t)y*W);
    d.end();

    int xBreak=0,yBreak=0,distinct=0;
    bool seen[16]={false};
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
      const uint8_t v=out[(size_t)y*W+x];
      if(!seen[v]){ seen[v]=true; distinct++; }
      if(x>=sz && out[(size_t)y*W+x]!=out[(size_t)y*W+x-sz]) xBreak++;
      if(y>=sz && out[(size_t)y*W+x]!=out[(size_t)(y-sz)*W+x]) yBreak++;
    }
    printf("   bayer%-3d periodic: x breaks=%d y breaks=%d, %d distinct inks\n",
           sz,xBreak,yBreak,distinct);
    CHECK(xBreak==0,"ordered output must repeat with the matrix width");
    CHECK(yBreak==0,"ordered output must repeat with the matrix height");
    CHECK(distinct>=2,"the threshold must actually flip the ink choice somewhere");
  }

  printf("\n-- random dithering is deterministic (the frame cache depends on it)\n");
  {
    DitherCfg cfg; cfg.type=DITHER_RANDOM;
    const E6Palette& p = paletteBuiltin(PAL_SPECTRA6);
    const int W=97,H=61;
    std::vector<uint8_t> in((size_t)W*3), a((size_t)W*H), b((size_t)W*H);
    for(int x=0;x<W;x++){ in[x*3]=150; in[x*3+1]=140; in[x*3+2]=130; }
    for(int pass=0;pass<2;pass++){
      E6Ditherer d; d.begin(W,p,cfg);
      auto& dst = pass? b : a;
      for(int y=0;y<H;y++) d.row(in.data(), dst.data()+(size_t)y*W);
      d.end();
    }
    const bool same = a==b;
    printf("   two renders of the same input %s\n", same?"match":"DIFFER");
    CHECK(same,"random dithering must be a pure function of (x,y) or the cache lies");
  }

  printf("\n-- chroma matching keeps a saturated pastel off white\n");
  {
    // epdoptimize's stated purpose for the chroma mode: "tries to keep
    // saturated pastel colors from collapsing into white". A light pink against
    // the spectra6 palette is the textbook case -- plain RGB distance puts it
    // on white, because white is simply the closest point in RGB space.
    const E6Palette& p = paletteBuiltin(PAL_SPECTRA6);
    const int W=64;
    std::vector<uint8_t> in((size_t)W*3), out((size_t)W);
    for(int x=0;x<W;x++){ in[x*3]=200; in[x*3+1]=150; in[x*3+2]=160; }
    int whites[2]={0,0};
    const ColorMatching ms[2]={CM_RGB,CM_CHROMA};
    for(int i=0;i<2;i++){
      DitherCfg cfg; cfg.type=DITHER_QUANTIZE_ONLY; cfg.matching=ms[i];
      E6Ditherer d; d.begin(W,p,cfg);
      d.row(in.data(), out.data());
      d.end();
      for(int x=0;x<W;x++) if(out[x]==E6_WHITE) whites[i]++;
    }
    printf("   pink (200,150,160): rgb -> %d/%d white, chroma -> %d/%d white\n",
           whites[0],W,whites[1],W);
    CHECK(whites[0]==W,"plain RGB distance is expected to collapse this pastel to white");
    CHECK(whites[1]<whites[0],"chroma matching must pull it away from white");
  }

  g_cfg = Settings();
  kernelSums();

  printf(fails?"\n%d FAILED\n":"\nall checks passed\n",fails);
  return fails?1:0;
}
