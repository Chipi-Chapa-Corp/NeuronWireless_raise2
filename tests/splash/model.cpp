#include <cassert>
#include <initializer_list>
#include <iostream>
#include <set>
#include "SplashHeatmap.h"
using namespace Communications_protocol;
int main() {
  SplashHeatmap heat;
  static_assert(SplashHeatmap::frame_ms == 20, "50 fps frame cap");
  heat.reset(0);heat.press(0,0,5);
  assert(heat.value(0,0)==48);
  assert(heat.value(0,1)==24 && heat.value(0,8)==24);
  assert(heat.value(0,2)==12 && heat.value(0,15)==12);
  assert(heat.value(0,3)==6 && heat.value(0,4)==3);
  assert(heat.energy(0,5)==384 && heat.energy(0,6)==192);
  for(unsigned i=0;i<36;++i) assert(heat.energy(1,i)==0);
  heat.press(0,0,5);assert(heat.value(0,0)==96 && heat.value(0,1)==48);
  heat.press(0,0,5);assert(heat.value(0,0)==144 && heat.value(0,1)==72);
  for(unsigned i=0;i<3;++i) heat.press(0,0,5);
  assert(heat.value(0,0)==255 && heat.value(0,1)==144);

  heat.reset(0);heat.press(0,0,5);heat.tick(10);
  assert(heat.energy(0,0)>=12238 && heat.energy(0,0)<=12240);
  heat.tick(1750);assert(heat.value(0,0)>=23 && heat.value(0,0)<=25);
  heat.tick(5000);assert(heat.value(0,0)>=6 && heat.value(0,0)<=7);
  heat.tick(12000);assert(heat.energy(0,0)==0);
  heat.press(0,15,12000);assert(heat.value(1,0)==48 && heat.value(0,0)==0);
  heat.press(0,8,12000);heat.press(65535,65535,12000);
  assert(heat.value(1,0)==48);
  for(unsigned i=0;i<100;++i) heat.press(0,15,12000);
  assert(heat.value(1,0)==255);
  heat.reset(UINT32_MAX-9);heat.press(0,0,UINT32_MAX-9);heat.tick(0);
  assert(heat.energy(0,0)>=12238 && heat.energy(0,0)<=12240);
  heat.tick(UINT32_MAX-10);assert(heat.energy(0,0)==0);

  // Compare r5 decay at five times the elapsed time against r4's exact law.
  heat.reset(0);for(unsigned i=0;i<6;++i) heat.press(0,0,0);
  uint32_t old_energy=255*256;
  for(unsigned step=1;step<=300;++step) {
    old_energy=old_energy*251/256;
    heat.tick(step*50);
    int difference=static_cast<int>(heat.energy(0,0))-static_cast<int>(old_energy);
    assert(difference>-256 && difference<256);
  }
  heat.reset(0);heat.press(4,5,0);
  assert(heat.value(0,31)==48 && heat.value(0,32)==24);
  assert(heat.value(0,30)==24 && heat.value(0,29)==24 && heat.value(0,26)==3);

  // Check actual emitted RGB, not time-averaged palette indices. A single
  // splash must fade through many real colors without alternating fixed shades.
  heat.reset(0);heat.press(0,0,0);
  std::set<uint32_t> shades;
  uint8_t previous_rgb[4]{};
  SplashHeatmap::Frame frame{},repeat{};
  for(unsigned ms=0;ms<=12000;ms+=20) {
    heat.tick(ms);heat.make_frame(0,33,frame);heat.make_frame(0,33,repeat);
    assert(memcmp(&frame,&repeat,sizeof(frame))==0);
    for(unsigned i=0;i<33;++i) {
      uint8_t expected[4];SplashHeatmap::color_energy(heat.energy(0,i),expected);
      assert(frame.indices[i]<16);
      assert(memcmp(frame.palette[frame.indices[i]],expected,4)==0);
    }
    const auto &rgb=frame.palette[frame.indices[0]];
    shades.insert((rgb[0]<<16)|(rgb[1]<<8)|rgb[2]);
    if(ms) for(unsigned channel=0;channel<4;++channel) {
      int delta=static_cast<int>(rgb[channel])-previous_rgb[channel];
      assert(delta>=-2 && delta<=2);
    }
    memcpy(previous_rgb,rgb,4);
  }
  assert(shades.size()>=35);
  std::cout<<"Single-tap RGB fade: "<<shades.size()<<" distinct displayed colors, <=2/255 channel change per 20 ms\n";
  assert(previous_rgb[0]==0 && previous_rgb[1]==0);

  // Repeated taps saturate the center, but its fade still has many RGB steps.
  heat.reset(0);for(unsigned i=0;i<20;++i) heat.press(0,0,0);
  std::set<uint32_t> hot_shades;
  memset(previous_rgb,0,4);
  for(unsigned ms=0;ms<=17000;ms+=20) {
    heat.tick(ms);heat.make_frame(0,33,frame);
    const auto &rgb=frame.palette[frame.indices[0]];
    hot_shades.insert((rgb[0]<<16)|(rgb[1]<<8)|rgb[2]);
    if(ms) for(unsigned c=0;c<4;++c) {
      int delta=static_cast<int>(rgb[c])-previous_rgb[c];
      assert(delta>=-4 && delta<=4);
    }
    memcpy(previous_rgb,rgb,4);
  }
  assert(hot_shades.size()>=200);
  std::cout<<"Repeated-tap saturated fade: "<<hot_shades.size()<<" RGB colors, <=4/255 channel change per 20 ms\n";

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
  assert(rgba[0] == 255 && rgba[1] == 1 && rgba[2] == 0 && rgba[3] == 0);

  std::cout << "Heatmap model and packet encoding passed\n";
}
