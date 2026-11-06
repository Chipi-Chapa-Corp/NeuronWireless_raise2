// Compile the actual Communications::sendPacket routing body against fake queues.
#include <cassert>
#include "Communications.h"
struct Side {
  unsigned calls=0;
  bool accepted=true;
  Devices device=UNKNOWN;
  Devices connectedDevice() { return device; }
  bool sendPacket(Packet &) { ++calls;return accepted; }
} comN2SideLeft,comN2SideRight;
#include "actual_send.inc"
#include "actual_route.inc"
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
  comN2SideLeft.device=KEYSCANNER_DEFY_LEFT;comN2SideRight.device=KEYSCANNER_DEFY_RIGHT;
  assert(transport.connectedSideDevice(KEYSCANNER_DEFY_LEFT)==KEYSCANNER_DEFY_LEFT);
  assert(transport.connectedSideDevice(KEYSCANNER_DEFY_RIGHT)==KEYSCANNER_DEFY_RIGHT);
  comN2SideLeft.device=RF_DEFY_LEFT;comN2SideRight.device=UNKNOWN;
  assert(transport.connectedSideDevice(KEYSCANNER_DEFY_LEFT)==RF_DEFY_LEFT);
  assert(transport.connectedSideDevice(KEYSCANNER_DEFY_RIGHT)==UNKNOWN);
  comN2SideLeft.device=BLE_DEFY_LEFT;
  assert(transport.connectedSideDevice(KEYSCANNER_DEFY_RIGHT)==BLE_DEFY_RIGHT);
  comN2SideLeft.device=UNKNOWN;comN2SideRight.device=BLE_DEFY_RIGHT;
  assert(transport.connectedSideDevice(KEYSCANNER_DEFY_LEFT)==BLE_DEFY_LEFT);
  comN2SideLeft.device=KEYSCANNER_DEFY_LEFT;
  assert(transport.connectedSideDevice(KEYSCANNER_DEFY_LEFT)==KEYSCANNER_DEFY_LEFT);
  assert(transport.connectedSideDevice(UNKNOWN)==UNKNOWN);
}
