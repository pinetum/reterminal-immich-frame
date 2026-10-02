#include "settings.h"
#include "render.h"
#include "log.h"
#include <cstdio>
#include <cmath>

Settings g_cfg;
void settingsLoad(){} void settingsSave(){} void settingsFactoryReset(){}
const char* imageSizeName(ImageSize){ return "preview"; }
const char* ditherName(DitherMethod){ return "test"; }
void logBegin(){} void logMem(const char*){}

static inline uint8_t nib(const uint8_t*f,int x,int y){
  const uint8_t b=f[(size_t)y*PANEL_STRIDE+(x>>1)]; return (x&1)?(b&0xF):(b>>4);
}
static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("  FAIL: %s\n",m); fails++; } }while(0)

// The invariant that actually defines error diffusion: averaged over an area,
// the palette colours chosen must reconstruct the input colour. The Spectra 6
// palette has no greys, so a flat grey is reproduced by MIXING green/red/blue/
// yellow/black/white -- which is exactly the thing worth testing.
struct Res { double r,g,b; double worstRowErr; int worstRow; };

static Res run(DitherMethod m,int R,int G,int B){
  g_cfg.dither=m; g_cfg.gamma=1.0f; g_cfg.fit=FIT_CONTAIN; g_cfg.rotation=0;
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
      uint8_t pr,pg,pb; e6CodeToRgb(nib(f,x,y),&pr,&pg,&pb);
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

static void check(DitherMethod m,const char* name,int R,int G,int B,
                  double meanTol,double rowTol){
  const Res x=run(m,R,G,B);
  const double err=(fabs(x.r-R)+fabs(x.g-G)+fabs(x.b-B))/3.0;
  printf("%-9s in=(%3d,%3d,%3d) out=(%6.1f,%6.1f,%6.1f) meanErr=%5.1f  worstRow=%5.1f @y=%d\n",
         name,R,G,B,x.r,x.g,x.b,err,x.worstRowErr,x.worstRow);
  CHECK(err<meanTol,"mean reconstructed colour must track the input");
  CHECK(x.worstRowErr<rowTol,"no row may drift (would mean the rolling error buffer is wrong)");
}

int main(){
  // Tolerances, and why they are what they are:
  //
  // meanTol: how far the area-averaged reconstruction may sit from the input.
  //   Floyd-Steinberg and Jarvis conserve all of the error, so they land within
  //   a few units of 255 -- about 1-2%. It is slightly looser near the top of
  //   the range (200,200,200) because the palette is sparse up there and the
  //   clamp to 0..255 before quantising gives a little error back.
  //
  // rowTol: a single row's mean may legitimately wander on a FLAT field,
  //   because error diffusion builds structured patterns that are not
  //   row-balanced -- rows are not independent samples. What this bound is
  //   really guarding is the rolling 3-row error buffer: if its rotation or its
  //   clearing were wrong, rows would be off by 100+, not 25. See
  //   test_stream.cpp, which proves that buffer exactly rather than statistically.
  check(DITHER_FS,    "fs grey",  128,128,128,  6.0, 40.0);
  check(DITHER_FS,    "fs dark",   64, 64, 64,  6.0, 40.0);
  check(DITHER_FS,    "fs light", 200,200,200,  6.0, 40.0);
  check(DITHER_FS,    "fs skin",  222,170,140,  6.0, 40.0);
  check(DITHER_JARVIS,"jarvis",   128,128,128,  6.0, 40.0);
  // Atkinson deliberately discards 2/8 of the error for extra contrast, so it is
  // expected to sit further from the input. Bounded, not ignored.
  check(DITHER_ATKINSON,"atkinson",128,128,128, 40.0, 60.0);
  printf(fails?"\n%d FAILED\n":"\nall checks passed\n",fails);
  return fails?1:0;
}
