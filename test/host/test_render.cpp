#include "settings.h"
#include "render.h"
#include "log.h"
#include <cassert>
#include <cstdio>
#include <set>

Settings g_cfg;
void settingsLoad(){} void settingsSave(){} void settingsFactoryReset(){}
const char* imageSizeName(ImageSize){ return "preview"; }
const char* ditherName(DitherMethod){ return "test"; }
void logBegin(){} void logMem(const char*){}

static inline uint8_t getNib(const uint8_t* f,int px,int py){
  const uint8_t b=f[(size_t)py*PANEL_STRIDE+(px>>1)];
  return (px&1)? (b&0x0F) : (b>>4);
}
static uint16_t rgb565(int r,int g,int b){
  return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}
// Exact RGB565 encodings of the palette so DITHER_NONE round-trips cleanly.
static const uint16_t RED = rgb565(229,57,53), GREEN = rgb565(29,185,84),
                      BLUE = rgb565(0,76,255), BLACK = rgb565(0,0,0),
                      WHITE= rgb565(255,255,255);

static SrcImage mk(int w,int h,uint16_t fill){
  SrcImage s; s.w=w; s.h=h; s.px=(uint16_t*)malloc((size_t)w*h*2);
  for(size_t i=0;i<(size_t)w*h;i++) s.px[i]=fill;
  return s;
}
static void box(SrcImage&s,int x0,int y0,int w,int h,uint16_t c){
  for(int y=y0;y<y0+h;y++) for(int x=x0;x<x0+w;x++) s.px[(size_t)y*s.w+x]=c;
}
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){ printf("  FAIL: %s\n",msg); fails++; } }while(0)

int main(){
  g_cfg.dither=DITHER_NONE; g_cfg.gamma=1.0f; g_cfg.fit=FIT_CONTAIN;
  uint8_t* f=frameAlloc();

  // ---- 1. rotation 0, 1:1, identity ---------------------------------------
  {
    g_cfg.rotation=0;
    SrcImage s=mk(PANEL_W,PANEL_H,WHITE);
    box(s,0,0,100,100,RED);                      // source top-left
    box(s,PANEL_W-100,PANEL_H-100,100,100,BLUE); // source bottom-right
    RenderStats st; assert(renderFrame(s,f,&st));
    printf("rot0: rot=%d canvas=%dx%d\n",st.rotation,st.logicalW,st.logicalH);
    CHECK(getNib(f,10,10)==E6_RED,  "rot0 top-left should be red");
    CHECK(getNib(f,PANEL_W-10,PANEL_H-10)==E6_BLUE,"rot0 bottom-right should be blue");
    CHECK(getNib(f,600,800)==E6_WHITE,"rot0 centre should be white");
    // nibble packing: an odd-x pixel must land in the low nibble
    box(s,0,0,PANEL_W,PANEL_H,WHITE); box(s,1,0,1,1,BLACK);
    assert(renderFrame(s,f,&st));
    CHECK((f[0]&0x0F)==E6_BLACK,"odd x must be the low nibble");
    CHECK((f[0]>>4)==E6_WHITE,  "even x must be the high nibble");
    srcFree(&s);
  }

  // ---- 2. rotation 270 = frame turned clockwise ---------------------------
  // Panel is portrait. Turning the device 90 deg clockwise makes the panel's
  // +x axis point DOWN for the viewer and its +y axis point LEFT. So the
  // viewer's top-left must land at panel (px=0, py=PANEL_H-1).
  {
    g_cfg.rotation=270;
    SrcImage s=mk(PANEL_H,PANEL_W,WHITE);        // 1600x1200 landscape source
    box(s,0,0,100,100,RED);                      // viewer top-left
    box(s,1500,0,100,100,GREEN);                 // viewer top-right
    box(s,0,1100,100,100,BLUE);                  // viewer bottom-left
    RenderStats st; assert(renderFrame(s,f,&st));
    printf("rot270: canvas=%dx%d\n",st.logicalW,st.logicalH);
    CHECK(getNib(f,10,PANEL_H-10)==E6_RED,  "rot270 viewer top-left -> panel (small x, max y)");
    CHECK(getNib(f,10,10)==E6_GREEN,        "rot270 viewer top-right -> panel (small x, small y)");
    CHECK(getNib(f,PANEL_W-10,PANEL_H-10)==E6_BLUE,"rot270 viewer bottom-left -> panel (max x, max y)");
    srcFree(&s);
  }

  // ---- 3. rotation 90 must be exactly the opposite turn -------------------
  {
    g_cfg.rotation=90;
    SrcImage s=mk(PANEL_H,PANEL_W,WHITE);
    box(s,0,0,100,100,RED);                      // viewer top-left
    RenderStats st; assert(renderFrame(s,f,&st));
    CHECK(getNib(f,PANEL_W-10,10)==E6_RED,"rot90 viewer top-left -> panel (max x, small y)");
    srcFree(&s);
  }

  // ---- 4. rotation 180 -----------------------------------------------------
  {
    g_cfg.rotation=180;
    SrcImage s=mk(PANEL_W,PANEL_H,WHITE);
    box(s,0,0,100,100,RED);
    RenderStats st; assert(renderFrame(s,f,&st));
    CHECK(getNib(f,PANEL_W-10,PANEL_H-10)==E6_RED,"rot180 top-left -> bottom-right");
    srcFree(&s);
  }

  // ---- 5. COVER must leave no white gap; CONTAIN must letterbox -----------
  {
    g_cfg.rotation=0;
    g_cfg.fit=FIT_COVER;
    SrcImage s=mk(400,200,RED);                  // very wide source
    RenderStats st; assert(renderFrame(s,f,&st));
    size_t white=0;
    for(int y=0;y<PANEL_H;y++) for(int x=0;x<PANEL_W;x++) if(getNib(f,x,y)==E6_WHITE) white++;
    printf("cover: %zu white px of %d\n",white,PANEL_W*PANEL_H);
    CHECK(white==0,"COVER must fill the whole panel");

    g_cfg.fit=FIT_CONTAIN;
    assert(renderFrame(s,f,&st));
    white=0;
    for(int y=0;y<PANEL_H;y++) for(int x=0;x<PANEL_W;x++) if(getNib(f,x,y)==E6_WHITE) white++;
    const size_t expect=(size_t)PANEL_W*PANEL_H-(size_t)PANEL_W*(PANEL_W*200/400);
    printf("contain: %zu white px, expected about %zu\n",white,expect);
    CHECK(white>expect*9/10 && white<expect*11/10,"CONTAIN must letterbox about the right amount");
    srcFree(&s);
  }

  // ---- 6. auto picks landscape for a wide photo, portrait for a tall one --
  {
    g_cfg.rotation=ROTATION_AUTO; g_cfg.fit=FIT_COVER;
    RenderStats st;
    SrcImage w=mk(800,600,RED);  assert(renderFrame(w,f,&st));
    printf("auto landscape -> rot=%d canvas=%dx%d\n",st.rotation,st.logicalW,st.logicalH);
    CHECK(st.logicalW==PANEL_H&&st.logicalH==PANEL_W,"auto: wide photo uses the landscape canvas");
    CHECK(st.rotation==270,"auto: wide photo turns clockwise (270)");
    srcFree(&w);
    SrcImage t=mk(600,800,RED);  assert(renderFrame(t,f,&st));
    printf("auto portrait  -> rot=%d canvas=%dx%d\n",st.rotation,st.logicalW,st.logicalH);
    CHECK(st.rotation==0,"auto: tall photo stays portrait");
    srcFree(&t);
  }

  frameFree(f);
  printf(fails? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", fails);
  return fails?1:0;
}
