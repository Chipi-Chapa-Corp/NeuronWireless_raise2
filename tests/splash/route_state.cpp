// Exercise the real ComSide/ComModel and stock connection-cache callbacks.
#include <cassert>
#include <cstring>
#include <deque>
#include <iostream>
#include "Communications.h"
#include "Communications_side.h"
MockCommunications Communications;
struct LEDSwitch { void enable() {} } LEDControl;
bool ble_innited() { return false; }
Devices leftConnection[3]{},rightConnection[3]{};
struct FakeHand { void releaseAllKeys() {} };
struct KeyboardHands {
  static FakeHand leftHand,rightHand;
  static void setup();
};
FakeHand KeyboardHands::leftHand,KeyboardHands::rightHand;
void KeyboardHands::setup() {
#include "actual_connection_callbacks.inc"
}
struct KeyboardKeyScanner { static Devices leftHandDevice(); };
#include "actual_connection_getter.inc"

struct Port {
  bool connected=true;
  Devices device=KEYSCANNER_DEFY_LEFT;
  std::deque<Packet> incoming;
  std::vector<Packet> outgoing;
  static void event(void *,const ComModel::com_model_event_cb_config_t *) {}
  static bool send(void *self,Packet &p) { static_cast<Port*>(self)->outgoing.push_back(p);return true; }
  static bool read(void *self,Packet &p) {
    auto &port=*static_cast<Port*>(self);
    if(port.incoming.empty()) return false;
    p=port.incoming.front();port.incoming.pop_front();return true;
  }
  static bool alive(void *self) { return static_cast<Port*>(self)->connected; }
  static void disconnect(void *) {}
  static Devices route(void *self) { return static_cast<Port*>(self)->device; }
};

int main() {
  KeyboardHands::setup();
  Port port;
  const ComModel::com_model_if_t interface={Port::event,Port::send,Port::read,Port::alive,Port::disconnect,Port::route};
  ComModel model;
  const ComModel::com_model_config_t config={&port,&interface};
  assert(model.init(&config));
  ComSide side;side.init();
  assert(side.connectedDevice()==UNKNOWN);
  assert(side.connect(&model));
  assert(side.connectedDevice()==UNKNOWN); // Handshake is not complete.
  Packet connected{};connected.header.command=CONNECTED;connected.header.device=KEYSCANNER_DEFY_LEFT;
  port.incoming.push_back(connected);side.run();side.run();
  assert(side.connectedDevice()==KEYSCANNER_DEFY_LEFT);
  assert(KeyboardKeyScanner::leftHandDevice()==KEYSCANNER_DEFY_LEFT);
  // A disconnect notification can clear the application's separate route cache
  // without changing the underlying connected ComSide. Reproduce this state;
  // this does not claim which notification occurred on the physical keyboard.
  Packet notice=connected;notice.header.command=DISCONNECTED;
  port.incoming.push_back(notice);side.run();
  assert(KeyboardKeyScanner::leftHandDevice()==UNKNOWN);
  assert(side.is_connected() && side.connectedDevice()==KEYSCANNER_DEFY_LEFT);
  unsigned keys=0;
  Communications.callbacks.bind(HAS_KEYS,[&](const Packet &p) { assert(p.data[2]==4);++keys; });
  Packet key=connected;key.header.command=HAS_KEYS;key.header.size=5;key.data[2]=4;
  for(unsigned n=0;n<10;++n) {port.incoming.push_back(key);side.run();}
  assert(keys==10 && KeyboardKeyScanner::leftHandDevice()==UNKNOWN);
  Packet led{};led.header.device=side.connectedDevice();led.header.command=MODE_LED;
  assert(side.sendPacket(led));
  port.connected=false;
  assert(side.connectedDevice()==UNKNOWN); // No stale live destination.
  side.run();side.run();
  assert(side.connectedDevice()==UNKNOWN && side.is_disconnected());
  std::cout<<"Actual ComSide + stock connection callbacks: UNKNOWN scanner route with live key input reproduced; active transport getter remains correct\n";
}
