#include "Arduino.h"
SerialT Serial1;
ESPClass ESP;
// decode.cpp pulls in JPEGDEC/PNGdec, so srcFree is stubbed for the host test.
#include "decode.h"
void srcFree(SrcImage* i){ if(i&&i->px){ free(i->px); i->px=nullptr; i->w=i->h=0; } }
