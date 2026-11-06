#pragma once
#include <stdint.h>
struct MockColormap {
 void getLayer(uint8_t layer, uint8_t *output) {
   for (unsigned i=0;i<176;++i) output[i]=(i+layer)%16;
 }
};
extern MockColormap ColormapEffectDefy;
