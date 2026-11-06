#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>
#include "SplashHeatmapPlugin.h"
#include "Communications.h"
#include "Colormap-Defy.h"
#include "LED-Palette-Theme-Defy.h"
#include "Kaleidoscope-FocusSerial.h"
#include "LedModeSerializable-Layer.h"
#include "LedModeSerializable-SolidColor.h"

MockCommunications Communications;
kaleidoscope::MockRuntime kaleidoscope::Runtime;
MockLEDControl LEDControl;
MockColormap ColormapEffectDefy;
MockPalette LEDPaletteThemeDefy;
MockFocus Focus;
uint32_t test_time = 0;
using namespace Communications_protocol;

struct Display {
  uint8_t palette[16][4]{}, maps[10][36]{}, rgb[36][4]{};
  void apply(const Packet &p) {
    if(p.header.command==PALETTE_COLORS) {
      assert(p.header.size>=1 && (p.header.size-1)%4==0);
      assert(p.data[0]+(p.header.size-1)/4<=16);
      memcpy(palette[p.data[0]],p.data+1,p.header.size-1);
    } else if(p.header.command==LAYER_KEYMAP_COLORS) {
      assert(p.header.size==19 && p.data[0]<10);
      for(unsigned i=0;i<36;++i) maps[p.data[0]][i]=(p.data[1+i/2]>>((i%2)*4))&15;
    } else if(p.header.command==MODE_LED) {
      uint32_t id;memcpy(&id,p.data,4);
      if(id==CRC32_STR("LedModeSerializable_Layer")) {
        assert(p.data[8]==0 && p.data[9]==0);
        for(unsigned i=0;i<36;++i) memcpy(rgb[i],palette[maps[p.data[8]][i]],4);
      } else if(id==CRC32_STR("LedModeSerializable_SolidColor")) {
        for(auto &color:rgb) memcpy(color,p.data+8,4);
      }
    }
  }
} displays[2];
struct Draw { unsigned time,side;uint8_t rgb[4]; };
std::vector<Draw> draws;

void mock_activate_mode() {
  Packet p{};p.header.command=MODE_LED;p.header.size=10;
  p.data[8]=7;p.data[9]=1; // Deliberately different from the overlay.
  Communications.sendPacket(p);
}
void step(unsigned cycles=1,bool paced=true) {
  for(unsigned i=0;i<cycles;++i) {
    ++test_time;
    size_t begin=Communications.packets.size();
    SplashHeatmapLED.render();
    if(paced) assert(Communications.packets.size()-begin<=2);
    for(size_t n=begin;n<Communications.packets.size();++n) {
      const auto &p=Communications.packets[n];
      for(unsigned side=0;side<2;++side) {
        if(p.header.device!=(side?KEYSCANNER_DEFY_RIGHT:KEYSCANNER_DEFY_LEFT) && p.header.device!=UNKNOWN) continue;
        displays[side].apply(p);
        uint32_t id=0;memcpy(&id,p.data,4);
        if(p.header.command==MODE_LED && id==CRC32_STR("LedModeSerializable_Layer")) {
          Draw draw{test_time,side,{}};memcpy(draw.rgb,displays[side].rgb[8],4);draws.push_back(draw);
        }
      }
    }
  }
}
void command(unsigned value) {
  Focus.eol=false;Focus.input=value;
  assert(SplashHeatmapLED.onFocusEvent("splash2")==kaleidoscope::EventHandlerResult::EVENT_CONSUMED);
}
void assert_owned() {
  for(const auto &p:Communications.packets) {
    assert(p.header.size<=28);
    if(p.header.command==MODE_LED) {
      uint32_t id;memcpy(&id,p.data,4);
      assert(id==CRC32_STR("LedModeSerializable_Layer") || id==CRC32_STR("LedModeSerializable_SolidColor"));
      if(id==CRC32_STR("LedModeSerializable_Layer")) assert(p.data[8]==0 && p.data[9]==0);
    }
  }
}
int main(int argc,char **argv) {
  auto &plugin=SplashHeatmapLED;plugin.onSetup();command(1);
  Communications.reject=true;step(10);assert(Communications.packets.empty());
  Communications.reject=false;step(25);assert(Communications.packets.size()==50);
  for(unsigned side=0;side<2;++side) {
    bool bl[10]{},ug[10]{};
    for(const auto &p:Communications.packets) {
      if(p.header.device!=(side?KEYSCANNER_DEFY_RIGHT:KEYSCANNER_DEFY_LEFT)) continue;
      if(p.header.command==LAYER_KEYMAP_COLORS) {bl[p.data[0]]=true;assert(p.header.size==19);}
      if(p.header.command==LAYER_UNDERGLOW_COLORS) {ug[p.data[0]]=true;assert(p.header.size==28);}
    }
    for(unsigned layer=0;layer<10;++layer) assert(bl[layer] && ug[layer]);
  }
  step(5); // Palette chunks -> matching map -> one explicit redraw.
  assert(Communications.packets.size()==60);
  size_t before=Communications.packets.size();
  for(unsigned cycle=0;cycle<100;++cycle) {
    for(auto c:{MODE_LED,PALETTE_COLORS,LAYER_KEYMAP_COLORS,LAYER_UNDERGLOW_COLORS,LED}) {
      Packet p{};p.header.command=c;p.header.size=10;p.data[8]=9;p.data[9]=1;
      assert(Communications.sendPacket(p));
    }
    plugin.onLayerChange();plugin.onLEDModeChange();step();
  }
  assert(Communications.packets.size()==before);
  Focus.eol=true;plugin.onFocusEvent("splash2");assert(Focus.output==1);
  for(auto c:{BRIGHTNESS,HOST_CONNECTION,SLEEP,WAKE_UP,HAS_KEYS,CONFIGURATION}) {
    Packet p{};p.header.command=c;assert(Communications.sendPacket(p));
    assert(Communications.packets.back().header.command==c);
  }
  Packet key{};key.header.command=HAS_KEYS;key.header.device=KEYSCANNER_DEFY_LEFT;
  key.header.size=5;key.data[1]=2;
  Packet original=key;Communications.callbacks.call(HAS_KEYS,key);
  assert(memcmp(&key,&original,sizeof(key))==0);
  step();assert(Communications.packets.back().header.command==PALETTE_COLORS);
  // A partially queued frame stays frozen and retries before drawing. Palette
  // writes alone must not affect the physical output buffer.
  for(auto channel:displays[0].rgb[8]) assert(channel==0);
  before=Communications.packets.size();Communications.reject=true;
  step(40);assert(Communications.packets.size()==before);
  Communications.reject=false;step(5);
  assert(displays[0].rgb[8][0]==18 && displays[0].rgb[8][1]==48);
  assert(displays[0].rgb[1][1]==24 && displays[0].rgb[3][1]==12);
  // Held snapshots cannot add heat, and the final black frame is redrawn.
  for(unsigned i=0;i<12500;++i) {Communications.callbacks.call(HAS_KEYS,key);step();}
  for(const auto &color:displays[0].rgb) for(auto channel:color) assert(channel==0);

  // Measure real RGB values reconstructed from the actual plugin's emitted
  // palette/map/MODE packets during an uninterrupted isolated tap.
  key.data[1]=0;Communications.callbacks.call(HAS_KEYS,key);
  key.data[1]=2;Communications.callbacks.call(HAS_KEYS,key);
  size_t draw_start=draws.size();step(12000);
  std::set<unsigned> colors;
  uint8_t previous[4]{};unsigned previous_time=0;
  for(size_t i=draw_start;i<draws.size();++i) {
    const auto &draw=draws[i];if(draw.side) continue;
    colors.insert((draw.rgb[0]<<16)|(draw.rgb[1]<<8)|draw.rgb[2]);
    if(previous_time) {
      assert(draw.time-previous_time>=SplashHeatmap::frame_ms);
      for(unsigned channel=0;channel<4;++channel) {
        int delta=static_cast<int>(draw.rgb[channel])-previous[channel];
        assert(delta<=0 && delta>=-3); // Actual channel fade, without dither reversals.
      }
    }
    memcpy(previous,draw.rgb,4);previous_time=draw.time;
  }
  assert(colors.size()>=35);
  std::cout<<"Actual plugin RGB wire trace: "<<colors.size()<<" distinct single-tap colors, monotone fade, <=3/255 channel steps\n";

  // Exercise the reported repeated-same-key case through the actual sender,
  // after all earlier snapshots have completed.
  step(30);
  for(unsigned i=0;i<20;++i) {
    key.data[1]=0;Communications.callbacks.call(HAS_KEYS,key);
    key.data[1]=2;Communications.callbacks.call(HAS_KEYS,key);
  }
  draw_start=draws.size();step(17000);
  std::set<unsigned> hot_colors;previous_time=0;
  for(size_t i=draw_start;i<draws.size();++i) {
    const auto &draw=draws[i];if(draw.side) continue;
    hot_colors.insert((draw.rgb[0]<<16)|(draw.rgb[1]<<8)|draw.rgb[2]);
    if(previous_time) for(unsigned c=0;c<4;++c) {
      int delta=static_cast<int>(draw.rgb[c])-previous[c];
      assert(delta>=-7 && delta<=7);
    }
    memcpy(previous,draw.rgb,4);previous_time=draw.time;
  }
  assert(hot_colors.size()>=190);
  std::cout<<"Actual repeated-tap RGB wire trace: "<<hot_colors.size()<<" colors, <=7/255 channel steps\n";

  Packet connected{};connected.header.command=CONNECTED;connected.header.device=KEYSCANNER_DEFY_LEFT;
  Communications.callbacks.call(CONNECTED,connected);before=Communications.packets.size();
  for(unsigned i=0;i<25;++i) {
    Packet retry=connected;retry.header.command=RETRY_LAYERS;
    Communications.callbacks.call(RETRY_LAYERS,retry);step();
  }
  unsigned reconnected_left=0;
  for(size_t n=before;n<Communications.packets.size();++n)
    reconnected_left+=Communications.packets[n].header.device==KEYSCANNER_DEFY_LEFT;
  assert(reconnected_left==25);step(5);
  Communications.connected=false;before=Communications.packets.size();step(50);
  assert(Communications.packets.size()==before);Communications.connected=true;
  LEDControl.enabled=false;step();
  uint32_t id;memcpy(&id,Communications.packets.back().data,4);
  assert(id==CRC32_STR("LedModeSerializable_SolidColor"));
  before=Communications.packets.size();step(50);assert(Communications.packets.size()==before);
  LEDControl.enabled=true;step(30);
  // A half can resume without CONNECTED; it asks for MODE_LED instead. Our
  // exclusive output filter must be paired with an overlay-owned response.
  Packet request{};request.header.command=MODE_LED;request.header.device=KEYSCANNER_DEFY_LEFT;
  before=Communications.packets.size();Communications.callbacks.call(MODE_LED,request);
  step(30);
  unsigned wake_packets=0;
  for(size_t n=before;n<Communications.packets.size();++n)
    wake_packets+=Communications.packets[n].header.device==KEYSCANNER_DEFY_LEFT;
  assert(wake_packets>=25);

  // With no changed heat, idle operation never resends a cached frame.
  before=Communications.packets.size();
  displays[0].rgb[8][1]=255;
  step(2010);
  assert(displays[0].rgb[8][1]==255);
  assert(Communications.packets.size()==before);
  // Removed diagnostic commands are unhandled and generate no LED traffic.
  for(const char *name:{"splash.stats","splash.transport","splash.refresh"}) {
    Focus.values.clear();
    assert(plugin.onFocusEvent(name)==kaleidoscope::EventHandlerResult::OK);
    assert(Focus.values.empty());
  }
  assert(Communications.packets.size()==before);
  command(1);step(30);
  assert(displays[0].rgb[8][1]==0);

  // A genuine cache disconnect stops writes, and reconnect initializes the
  // half. The transport/cache notification regression is tested in route_state.
  kaleidoscope::Runtime.scanner.left=UNKNOWN;
  before=Communications.packets.size();step(40);
  assert(Communications.packets.size()==before);
  kaleidoscope::Runtime.scanner.left=KEYSCANNER_DEFY_LEFT;step(30);

  // A blocked half must neither freeze its peer nor replay an ancient bright
  // snapshot once transport returns. Exercise behavior without counters.
  Communications.reject_device=KEYSCANNER_DEFY_LEFT;
  key.data[1]=0;Communications.callbacks.call(HAS_KEYS,key);
  key.data[1]=2;Communications.callbacks.call(HAS_KEYS,key);
  Packet right_key=key;right_key.header.device=KEYSCANNER_DEFY_RIGHT;
  Communications.callbacks.call(HAS_KEYS,right_key);
  before=Communications.packets.size();step(1500);
  unsigned peer_frames=0;
  for(size_t n=before;n<Communications.packets.size();++n) {
    const auto &p=Communications.packets[n];
    assert(p.header.device!=KEYSCANNER_DEFY_LEFT);
    peer_frames+=p.header.device==KEYSCANNER_DEFY_RIGHT && p.header.command==MODE_LED;
  }
  assert(peer_frames>0);
  Communications.reject_device=UNKNOWN;step(10);
  assert(displays[0].rgb[8][1]>0 && displays[0].rgb[8][1]<48);

  unsigned last_map=0,maps=0,last_mode[2]{};
  for(unsigned ms=0;ms<300;++ms) {
    key.data[1]=(ms%3==0)?2:0;Communications.callbacks.call(HAS_KEYS,key);
    mock_activate_mode();plugin.onLayerChange();
    size_t begin=Communications.packets.size();step();
    for(size_t n=begin;n<Communications.packets.size();++n) {
      const auto &p=Communications.packets[n];unsigned side=p.header.device==KEYSCANNER_DEFY_RIGHT;
      if(p.header.command==MODE_LED) {
        if(last_mode[side]) assert(test_time-last_mode[side]>=SplashHeatmap::frame_ms);
        last_mode[side]=test_time;
      }
      if(p.header.command==LAYER_KEYMAP_COLORS && !side) {
        if(maps) assert(test_time-last_map>=SplashHeatmap::frame_ms);
        last_map=test_time;++maps;
      }
    }
  }
  assert(maps>0 && maps<=16);assert_owned();
  if(argc>1) {
    std::ofstream stream(argv[1],std::ios::binary);
    for(const auto &p:Communications.packets)
      if(p.header.command==PALETTE_COLORS || p.header.command==LAYER_KEYMAP_COLORS ||
         p.header.command==LAYER_UNDERGLOW_COLORS || p.header.command==MODE_LED)
        stream.write(reinterpret_cast<const char*>(p.buf),32);
  }
  before=Communications.packets.size();command(0);
  Communications.reject=true;step(30,false);assert(Communications.packets.size()==before);
  Communications.reject=false;step(25,false);
  for(unsigned side=0;side<2;++side) {
    unsigned maps=0,colors=0;
    const unsigned offsets[]={0,33,69,122},counts[]={33,36,53,54};
    for(size_t n=before;n<Communications.packets.size();++n) {
      const auto &p=Communications.packets[n];
      if(p.header.device!=(side?KEYSCANNER_DEFY_RIGHT:KEYSCANNER_DEFY_LEFT)) continue;
      if(p.header.command==PALETTE_COLORS) {
        for(unsigned k=0;k<(p.header.size-1)/4;++k)
          for(unsigned c=0;c<4;++c) assert(p.data[1+k*4+c]==p.data[0]+k+c*16);
        ++colors;
      }
      if(p.header.command!=LAYER_KEYMAP_COLORS && p.header.command!=LAYER_UNDERGLOW_COLORS) continue;
      unsigned region=side+(p.header.command==LAYER_UNDERGLOW_COLORS?2:0);
      for(unsigned i=0;i<counts[region];++i)
        assert(((p.data[1+i/2]>>((i%2)*4))&15)==(offsets[region]+i+p.data[0])%16);
      ++maps;
    }
    assert(maps==20 && colors==3);
  }
  Focus.eol=true;plugin.onFocusEvent("splash2");assert(Focus.output==0);
  Packet stock{};stock.header.command=LAYER_KEYMAP_COLORS;
  before=Communications.packets.size();Communications.sendPacket(stock);
  assert(Communications.packets.size()==before+1);
  plugin.onFocusEvent("splash.protocol");assert(Focus.output==2);
  std::cout<<"Actual overlay lifecycle, LED ownership, palette-before-redraw ordering, queue retry and restoration passed\n";
}
