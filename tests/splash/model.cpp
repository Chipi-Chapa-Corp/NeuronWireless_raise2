#include <cassert>
#include <initializer_list>
#include <iostream>
#include <set>
#include <fstream>
#include "SplashHeatmap.h"
#include "color_reference.h"
#include "color.h"
#include <algorithm>
using namespace Communications_protocol;
int main(int argc,char **argv) {
  SplashHeatmap heat;
  static_assert(SplashHeatmap::frame_ms == 20, "50 fps frame cap");
  heat.reset(0);heat.press(0,0,5);
  assert(heat.value(0,0)==32);
  assert(heat.value(0,1)==16 && heat.value(0,8)==16);
  assert(heat.value(0,2)==8 && heat.value(0,15)==8);
  assert(heat.value(0,3)==4 && heat.value(0,4)==2);
  assert(heat.energy(0,5)==256 && heat.energy(0,6)==128);
  for(unsigned i=0;i<36;++i) assert(heat.energy(1,i)==0);
  heat.press(0,0,5);assert(heat.value(0,0)==64 && heat.value(0,1)==32);
  for(unsigned i=0;i<6;++i) heat.press(0,0,5);
  assert(heat.value(0,0)==255 && heat.value(0,1)==128);

  // Fixed cooling law, batched catch-up, long gaps and uint32 clock rollover.
  heat.reset(0);heat.press(0,0,0);heat.tick(24);assert(heat.value(0,0)==32);
  heat.tick(25);assert(heat.value(0,0)==31);
  heat.tick(400);assert(heat.value(0,0)==16);
  heat.tick(799);assert(heat.value(0,0)==1);
  heat.tick(800);assert(heat.energy(0,0)==0);
  heat.press(0,15,800);assert(heat.value(1,0)==32 && heat.value(0,0)==0);
  heat.press(65535,65535,800);assert(heat.value(1,0)==32);
  for(unsigned i=0;i<8;++i) heat.press(0,15,800);
  assert(heat.value(1,0)==255);heat.tick(7174);assert(heat.value(1,0)==1);
  heat.tick(7175);assert(heat.energy(1,0)==0);
  heat.reset(UINT32_MAX-24);heat.press(0,0,UINT32_MAX-24);heat.tick(0);
  assert(heat.value(0,0)==31);heat.tick(UINT32_MAX-25);assert(heat.energy(0,0)==0);
  heat.reset(0);heat.press(0,0,0);heat.tick(250);assert(heat.value(0,0)==22);
  heat.press(0,0,250);assert(heat.value(0,0)==54);heat.tick(275);assert(heat.value(0,0)==53);
  heat.reset(0);heat.press(4,5,0);
  assert(heat.value(0,31)==32 && heat.value(0,32)==16);
  assert(heat.value(0,30)==16 && heat.value(0,29)==16 && heat.value(0,26)==2);
  std::cout<<"QMK cooling: +32 heat, half-strength rings, -1 every 25 ms, single tap 800 ms, saturation 6375 ms; catch-up/reheat/rollover passed\n";

  // Compare all 256 heat levels with the unmodified pinned QMK color.c,
  // using its heatmap HSV equation and scale8 at the shared effect ceiling.
  for(unsigned level=0;level<=255;++level) {
    hsv_t hsv{};hsv.h=170-(level>85?level-85:0);hsv.s=255;
    hsv.v=(std::min(level,85u)*3*(SplashColor::common_max+1))>>8;
    rgb_t expected=hsv_to_rgb_nocie(hsv);
    uint8_t linear[4],raw[4],actual[4];
    SplashHeatmap::color_linear(level*256,linear);
    assert(linear[0]==expected.r && linear[1]==expected.g && linear[2]==expected.b && linear[3]==0);
    SplashHeatmap::color_energy(level*256,raw);post_gamma(raw,actual);
    if(level>=85) assert(std::max({actual[0],actual[1],actual[2]})==SplashColor::common_max);
    if(level<=85) assert(actual[0]==0 && actual[1]==0 && actual[3]==0);
    if(level==0) assert(actual[2]==0);
    if(level==32) assert(actual[2]>=50 && actual[2]<=54);
    if(level==85) assert(actual[2]==138);
    if(level==170) assert(actual[1]==138 && actual[0]==0 && actual[2]==0);
    if(level==255) assert(actual[0]==138 && actual[1]==0 && actual[2]==0);
  }
  std::cout<<"QMK HSV reference: all 256 heat levels match pinned upstream color.c; hot RGB peak 138, cold pure-blue fade, no cutoff\n";

  std::set<uint32_t> shades,hot_shades;
  uint8_t previous_rgb[4]{};
  SplashHeatmap::Frame frame{},repeat{};
  for(unsigned scenario=0;scenario<2;++scenario) {
    heat.reset(0);for(unsigned i=0;i<(scenario?8u:1u);++i) heat.press(0,0,0);
    memset(previous_rgb,0,4);
    for(unsigned ms=0;ms<=7000;ms+=20) {
      heat.tick(ms);heat.make_frame(0,33,frame);heat.make_frame(0,33,repeat);
      assert(memcmp(&frame,&repeat,sizeof(frame))==0);
      for(unsigned i=0;i<33;++i) {
        uint8_t expected[4];SplashHeatmap::color_energy(heat.energy(0,i),expected);
        assert(frame.indices[i]<16);
        assert(memcmp(frame.palette[frame.indices[i]],expected,4)==0);
      }
      uint8_t rgb[4];post_gamma(frame.palette[frame.indices[0]],rgb);
      (scenario?hot_shades:shades).insert((rgb[0]<<16)|(rgb[1]<<8)|rgb[2]);
      if(ms) assert(std::max({rgb[0],rgb[1],rgb[2]})<=std::max({previous_rgb[0],previous_rgb[1],previous_rgb[2]}));
      if(!scenario) assert(rgb[0]==0 && rgb[1]==0);
      memcpy(previous_rgb,rgb,4);
    }
    assert(previous_rgb[0]==0 && previous_rgb[1]==0 && previous_rgb[2]==0);
  }
  assert(shades.size()>=30 && hot_shades.size()>=180);
  std::cout<<"QMK model fade: "<<shades.size()<<" single-tap colors; "<<hot_shades.size()<<" saturated colors; no peak-brightness rebounds; final black\n";

  // Busy overlapping splashes still fit the shared per-half palette, without
  // out-of-range indices, heap allocation, or nondeterministic dither phases.
  heat.reset(0);
  for(unsigned step=0;step<300;++step) {
    unsigned side=step%2,row=(step/2)%5,col=(step*3)%8;
    heat.press(row,side?15-col:col,step*3);
    for(unsigned half=0;half<2;++half) {
      heat.make_frame(half,half?36:33,frame);
      for(unsigned i=0;i<(half?36u:33u);++i) assert(frame.indices[i]<16);
      assert(frame.palette[0][0]==0 && frame.palette[0][1]==0);
    }
  }

  // Replay all these frames through the released ARM renderer as well. The
  // actual physical-output buffer must agree with this independent LUT oracle.
  // Include both isolated and heavily overlapping histories, then no more taps.
  std::ofstream trace;
  if(argc>1) trace.open(argv[1],std::ios::binary);
  for(unsigned scenario=0;scenario<3;++scenario) {
    heat.reset(0);
    if(scenario==0) heat.press(1,1,0);
    if(scenario==1) for(unsigned n=0;n<20;++n) heat.press(1,1,0);
    if(scenario==2) for(unsigned n=0;n<200;++n)
      heat.press(n%5,(n*3)%8,n*13);
    const unsigned origin=scenario==2?2600:0;
    for(unsigned ms=0;ms<=17000;ms+=20) {
      heat.tick(origin+ms);heat.make_frame(0,33,frame);
      uint8_t expected[36][4]{};
      for(unsigned led=0;led<33;++led) post_gamma(frame.palette[frame.indices[led]],expected[led]);
      if(trace.is_open()) {
        trace.write(reinterpret_cast<const char*>(&frame),sizeof(frame));
        trace.write(reinterpret_cast<const char*>(expected),sizeof(expected));
      }
    }
  }
  // Every mapped LED appears exactly once and remains inside its physical half.
  for (uint8_t side = 0; side < 2; ++side) {
    bool seen[36]{};
    unsigned count = 0;
    for (uint8_t row = 0; row < 5; ++row)
      for (uint8_t col = 0; col < 8; ++col) {
        uint8_t index = SplashHeatmap::led(side, row, col);
        if (index == 255) continue;
        assert(index < (side ? 36 : 33) && !seen[index]);
        seen[index] = true;
        ++count;
      }
    assert(count == (side ? 36u : 33u));
  }

  uint8_t indices[54];
  for (unsigned i = 0; i < 54; ++i) indices[i] = i % 16;
  for (uint8_t count : {uint8_t(33), uint8_t(36), uint8_t(53), uint8_t(54)}) {
    Packet p = SplashHeatmap::layer_packet(KEYSCANNER_DEFY_LEFT, count > 36, indices, count);
    assert(p.header.size <= MAX_DATA_SIZE && p.data[0] == 0);
    for (uint8_t i = 0; i < count; ++i)
      assert(((p.data[1 + i / 2] >> ((i % 2) * 4)) & 15) == indices[i]);
    unsigned wire_count = count > 36 ? 54 : 36;
    assert(p.header.size==1+wire_count/2);
    for (unsigned i=count;i<wire_count;++i)
      assert(((p.data[1+i/2]>>((i%2)*4))&15)==0);
  }
  uint8_t rgba[4];
  SplashHeatmap::color_energy(0, rgba);
  assert(rgba[0] == 0 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 0);
  SplashHeatmap::color_energy(255*256u, rgba);
  uint8_t corrected[4];post_gamma(rgba,corrected);
  assert(corrected[0] == 138 && corrected[1] == 0 && rgba[2] == 0 && rgba[3] == 0);

  std::cout << "Heatmap model and packet encoding passed\n";
}
