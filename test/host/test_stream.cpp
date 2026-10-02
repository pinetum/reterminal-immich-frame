// Proves the streaming ditherer's 3-row rolling error buffer is equivalent to a
// plain whole-image implementation. If the buffer rotation, the clearing of the
// consumed row, or any dy offset were wrong, these would diverge.
#include "e6_dither.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct Rgb { int r,g,b; };
static const Rgb P[6]={{255,255,255},{29,185,84},{229,57,53},{255,216,0},{0,76,255},{0,0,0}};
static const uint8_t C[6]={0x0,0x2,0x6,0xB,0xD,0xF};
struct Tap{int dx,dy,num,den;};
static const Tap FS[]={{1,0,7,16},{-1,1,3,16},{0,1,5,16},{1,1,1,16}};
static const Tap JV[]={{1,0,7,48},{2,0,5,48},{-2,1,3,48},{-1,1,5,48},{0,1,7,48},{1,1,5,48},{2,1,3,48},
                       {-2,2,1,48},{-1,2,3,48},{0,2,5,48},{1,2,3,48},{2,2,1,48}};
static const Tap AT[]={{1,0,1,8},{2,0,1,8},{-1,1,1,8},{0,1,1,8},{1,1,1,8},{0,2,1,8}};
static int cl8(int v){return v<0?0:(v>255?255:v);}
static int16_t cle(int v){return (int16_t)(v<-4096?-4096:(v>4096?4096:v));}
static int nearest(int r,int g,int b){
  int best=0,bd=1<<30;
  for(int i=0;i<6;i++){int dr=r-P[i].r,dg=g-P[i].g,db=b-P[i].b;int d=dr*dr+dg*dg+db*db;
    if(d<bd){bd=d;best=i;}}
  return best;
}
// Whole-image reference: one full-size signed error plane, no rolling at all.
static void reference(const uint8_t* rgb,int W,int H,const Tap* K,int nk,uint8_t* out){
  std::vector<int16_t> err((size_t)W*H*3,0);
  for(int y=0;y<H;y++) for(int x=0;x<W;x++){
    const size_t o=((size_t)y*W+x)*3;
    const int r=cl8(rgb[o+0]+err[o+0]), g=cl8(rgb[o+1]+err[o+1]), b=cl8(rgb[o+2]+err[o+2]);
    const int q=nearest(r,g,b);
    out[(size_t)y*W+x]=C[q];
    const int er=r-P[q].r, eg=g-P[q].g, eb=b-P[q].b;
    if((er|eg|eb)==0) continue;
    for(int k=0;k<nk;k++){
      const int nx=x+K[k].dx, ny=y+K[k].dy;
      if(nx<0||nx>=W||ny<0||ny>=H) continue;
      const size_t no=((size_t)ny*W+nx)*3;
      err[no+0]=cle(err[no+0]+er*K[k].num/K[k].den);
      err[no+1]=cle(err[no+1]+eg*K[k].num/K[k].den);
      err[no+2]=cle(err[no+2]+eb*K[k].num/K[k].den);
    }
  }
}
int main(){
  const int W=211,H=157;                       // deliberately not multiples of 3
  std::vector<uint8_t> img((size_t)W*H*3);
  srand(12345);
  for(size_t i=0;i<img.size();i++) img[i]=rand()&0xFF;

  struct { DitherMethod m; const char* n; const Tap* k; int nk; } cases[]={
    {DITHER_FS,"fs",FS,4},{DITHER_JARVIS,"jarvis",JV,12},{DITHER_ATKINSON,"atkinson",AT,6}};

  int fails=0;
  for(auto& c : cases){
    std::vector<uint8_t> ref((size_t)W*H), got((size_t)W*H);
    reference(img.data(),W,H,c.k,c.nk,ref.data());
    E6Ditherer d; d.begin(W,c.m,1.0f);
    for(int y=0;y<H;y++) d.row(img.data()+(size_t)y*W*3, got.data()+(size_t)y*W);
    d.end();
    size_t diff=0; int firstY=-1;
    for(size_t i=0;i<ref.size();i++) if(ref[i]!=got[i]){ if(firstY<0) firstY=(int)(i/W); diff++; }
    printf("%-9s %zu/%zu pixels differ%s\n",c.n,diff,ref.size(),
           diff?"":"  -> streaming == whole-image, exactly");
    if(diff){ printf("  FAIL: first divergence at row %d\n",firstY); fails++; }
  }
  printf(fails?"\n%d FAILED\n":"\nall checks passed\n",fails);
  return fails?1:0;
}
