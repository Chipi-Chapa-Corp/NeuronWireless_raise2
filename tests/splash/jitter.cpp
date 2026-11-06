// Cold overlapping splashes must never brighten after all input stops.
#include <cassert>
#include <cstring>
#include <iostream>
#include <set>
#include "SplashHeatmap.h"
#include "color_reference.h"
int main(int argc,char **) {
  unsigned rebounds=0, fading_samples=0;
  for(unsigned seed=1;seed<=20;++seed) {
    SplashHeatmap h;h.reset(0);unsigned rng=seed;
    for(unsigned n=0;n<35;++n) {
      rng=rng*1664525u+1013904223u;unsigned row=(rng>>16)%5;
      rng=rng*1664525u+1013904223u;unsigned col=(rng>>16)%8;
      h.press(row,col,n*83);
    }
    const unsigned origin=35*83+2000;h.tick(origin);
    std::set<unsigned> energies;
    for(unsigned led=0;led<33;++led) {
      assert(h.energy(0,led)<120*256);energies.insert(h.energy(0,led));
    }
    assert(energies.size()>15); // Exercise actual palette reduction.
    SplashHeatmap::Frame frame{};uint8_t previous[36][4]{};
    for(unsigned ms=0;ms<7000;ms+=20) {
      h.tick(origin+ms);h.make_frame(0,33,frame);
      for(unsigned led=0;led<33;++led) {
        uint8_t rgb[4];post_gamma(frame.palette[frame.indices[led]],rgb);
        if(ms) {
          rebounds+=(rgb[0]>previous[led][0] || rgb[1]>previous[led][1]);
          ++fading_samples;
        }
        memcpy(previous[led],rgb,4);
      }
    }
  }
  if(argc>1) {
    assert(rebounds>0);
    std::cout<<"Original adaptive regrouping: "<<rebounds<<" post-gamma brightening reversals reproduced during uninterrupted decay\n";
  } else {
    assert(rebounds==0);
    std::cout<<"Stable palette groups: "<<fading_samples<<" post-gamma key samples fade without brightening reversals\n";
  }
}
