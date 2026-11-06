#pragma once
#include "Communications.h"
#include "Hand.h"
struct MockPalette {
 void getColorPalette(cRGB *colors) {
   for (unsigned i=0;i<16;++i) colors[i]={uint8_t(i),uint8_t(i+16),uint8_t(i+32),uint8_t(i+48)};
 }
 void updatePaletteCommunication(Packet &) {
   for (unsigned part=0;part<3;++part) {
     Packet p{}; p.header.command=PALETTE_COLORS;
     p.header.size=1; p.data[0]=part*6;
     Communications.sendPacket(p);
   }
 }
};
extern MockPalette LEDPaletteThemeDefy;
