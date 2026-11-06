#pragma once
#include "actual_color_transfer.inc"
inline void post_gamma(const uint8_t *wire, uint8_t *output) {
  output[0]=red_transfer[wire[0]];
  output[1]=green_transfer[wire[1]];
  output[2]=output[3]=0;
}
