// Exercise the real ComSide/ComModel and connection-cache callbacks.
#include <cassert>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include "Communications.h"
#include "Communications_side.h"
MockCommunications Communications;
struct LEDSwitch { void enable() {} } LEDControl;
bool bleHost=false;
bool ble_innited() { return bleHost; }
Devices leftConnection[3]{},rightConnection[3]{};
struct FakeHand { unsigned releases=0; void releaseAllKeys() { ++releases; } };
struct KeyboardHands {
  static FakeHand leftHand,rightHand;
  static void setup();
};
FakeHand KeyboardHands::leftHand,KeyboardHands::rightHand;
void KeyboardHands::setup() {
#include "actual_connection_callbacks.inc"
}
struct KeyboardKeyScanner { static Devices leftHandDevice(); static Devices rightHandDevice(); };
#include "actual_connection_getter.inc"
#include "actual_crc8.inc"

struct Port {
  bool connected=true;
  Devices device;
  ComModel::com_model_event_cb_config_t callback{};
  std::deque<Packet> incoming;
  std::vector<Packet> outgoing;
  static void event(void *self,const ComModel::com_model_event_cb_config_t *cb) { static_cast<Port*>(self)->callback=*cb; }
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
Packet packet(Commands command,Devices device) {
  Packet p{};p.header.command=command;p.header.device=device;return p;
}
void notify(Commands command,Devices device) {
  auto p=packet(command,device);Communications.callbacks.call(command,p);
}
struct Link {
  Port port;
  ComModel model;
  ComSide side;
  explicit Link(Devices device) {
    port.device=device;
    static const ComModel::com_model_if_t interface={Port::event,Port::send,Port::read,Port::alive,Port::disconnect,Port::route};
    const ComModel::com_model_config_t config={&port,&interface};
    assert(model.init(&config));assert(side.init());connect();
  }
  void connect() {
    port.connected=true;assert(side.connect(&model));side.run();
    receive(packet(CONNECTED,port.device));assert(side.is_connected());
  }
  void receive(const Packet &p) { port.incoming.push_back(p);side.run(); }
  void unplug() { port.connected=false;side.run();side.run();assert(side.is_disconnected()); }
};
unsigned disconnected=0,keys=0;
void clear() {
  for(unsigned n=0;n<3;++n) leftConnection[n]=rightConnection[n]=UNKNOWN;
  KeyboardHands::leftHand.releases=KeyboardHands::rightHand.releases=0;
  disconnected=keys=0;bleHost=false;
}
int main(int argc,char **argv) {
  KeyboardHands::setup();
  Communications.callbacks.bind(DISCONNECTED,[](const Packet &) { ++disconnected; });
  Communications.callbacks.bind(HAS_KEYS,[](const Packet &p) { assert(p.data[2]==4);++keys; });
  std::ifstream released;
  if(argc>1) released.open(argv[1],std::ios::binary);
  for(unsigned half=0;half<2;++half) {
    clear();
    const Devices wired=half?KEYSCANNER_DEFY_RIGHT:KEYSCANNER_DEFY_LEFT;
    const Devices other=half?KEYSCANNER_DEFY_LEFT:KEYSCANNER_DEFY_RIGHT;
    const Devices rf=half?RF_DEFY_RIGHT:RF_DEFY_LEFT;
    const Devices ble=half?BLE_DEFY_RIGHT:BLE_DEFY_LEFT;
    auto *cache=half?rightConnection:leftConnection;
    auto &hand=half?KeyboardHands::rightHand:KeyboardHands::leftHand;
    auto getter=half?KeyboardKeyScanner::rightHandDevice:KeyboardKeyScanner::leftHandDevice;
    notify(CONNECTED,other);
    Link link(wired);
    assert(cache[1]==wired && getter()==wired);
    auto notice=packet(DISCONNECTED,wired);
    if(argc>1) {
      assert(released.read(reinterpret_cast<char*>(notice.buf),sizeof(notice)));
      assert(notice.header.command==DISCONNECTED && notice.header.device==wired);
      auto checksum=notice.header.crc;notice.header.crc=0;
      assert(crc8(notice.buf,sizeof(Header)+notice.header.size)==checksum);
      notice.header.crc=checksum;
    }
    link.receive(notice);
    assert(getter()==wired && link.side.is_connected());
    assert(disconnected==0 && hand.releases==0);
    auto key=packet(HAS_KEYS,wired);key.header.size=5;key.data[2]=4;
    for(unsigned n=0;n<10;++n) link.receive(key);
    assert(keys==10 && getter()==wired);
    auto led=packet(MODE_LED,getter());assert(link.side.sendPacket(led));
    // Unrewritten wireless notifications must retain their own identity.
    notify(CONNECTED,ble);assert(cache[0]==ble && cache[1]==wired);
    link.receive(packet(DISCONNECTED,ble));
    assert(cache[0]==UNKNOWN && getter()==wired && hand.releases==0 && disconnected==1);
    notify(CONNECTED,rf);assert(cache[2]==rf);
    link.receive(packet(DISCONNECTED,rf));
    assert(cache[2]==UNKNOWN && getter()==wired && hand.releases==0 && disconnected==2);
    // A genuine transport loss still clears the route and releases held keys.
    link.unplug();assert(getter()==UNKNOWN && hand.releases==1 && disconnected==3);
    assert((half?KeyboardKeyScanner::leftHandDevice():KeyboardKeyScanner::rightHandDevice())==other);
    link.connect();assert(getter()==wired);
    // A model-requested reconnect follows the same local disconnect path.
    link.port.callback.event_cb(link.port.callback.p_instance,ComModel::COM_MODEL_EVENT_RECONNECT_NEEDED);
    link.side.run();link.side.run();
    assert(getter()==UNKNOWN && hand.releases==2 && disconnected==4);
    link.connect();assert(getter()==wired);
    // A peer restart re-handshakes without falsely clearing the wired cache.
    link.receive(packet(IS_ALIVE,wired));link.side.run();link.receive(packet(CONNECTED,wired));
    assert(link.side.is_connected() && getter()==wired && disconnected==4);
    link.side.disconnect();link.side.run();link.side.run();
    assert(getter()==UNKNOWN && hand.releases==3 && disconnected==5);
    // Genuine RF and BLE model notifications are still delivered.
    for(auto wireless:{rf,ble}) {
      clear();Link radio(wireless);assert(getter()==wireless);
      radio.receive(packet(DISCONNECTED,wireless));assert(getter()==UNKNOWN && disconnected==1);
      notify(CONNECTED,wireless);radio.unplug();assert(getter()==UNKNOWN && disconnected==2);
    }
    // In BLE-host mode the SPI slot intentionally holds a BLE route: clear it.
    clear();bleHost=true;notify(CONNECTED,wired);
    assert(cache[1]==ble);notify(DISCONNECTED,ble);assert(getter()==UNKNOWN && hand.releases==1);
  }
  std::cout<<"Actual ComSide + connection callbacks: live wired cache preserved; BLE/RF, cable loss, reconnect, restart and key release passed on both halves\n";
  if(argc>1) std::cout<<"Released side radio disconnect packets: valid CRC, no false wired disconnect, continuing keys and cached LED routing passed\n";
}
