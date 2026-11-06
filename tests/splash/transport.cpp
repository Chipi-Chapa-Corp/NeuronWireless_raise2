// Compile the actual Communications::sendPacket routing body against fake queues.
#include <cassert>
#include "Communications.h"
struct Side {
  unsigned calls=0;
  bool accepted=true;
  bool sendPacket(Packet &) { ++calls;return accepted; }
} comN2SideLeft,comN2SideRight;
#include "actual_send.inc"
bool filter(const Packet &p) { return p.header.command!=MODE_LED; }
int main() {
  class Communications transport;
  Packet p{};p.header.command=MODE_LED;
  transport.setPacketFilter(filter);
  assert(transport.sendPacket(p));
  assert(comN2SideLeft.calls==0 && comN2SideRight.calls==0);
  p.header.command=HAS_KEYS;
  assert(transport.sendPacket(p));
  assert(comN2SideLeft.calls==1 && comN2SideRight.calls==1);
  comN2SideLeft.accepted=comN2SideRight.accepted=false;
  assert(!transport.sendPacket(p));
  transport.setPacketFilter(nullptr);p.header.command=MODE_LED;
  comN2SideRight.accepted=true;
  assert(transport.sendPacket(p));
  assert(comN2SideLeft.calls==3 && comN2SideRight.calls==3);
}
