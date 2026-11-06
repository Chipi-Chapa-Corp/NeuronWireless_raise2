#pragma once
#include "Communications_protocol.h"
#include "Callback.h"
#include <vector>
using namespace Communications_protocol;
struct MockCommunications {
 BindingCallbacks<Communications_protocol::Commands, Communications_protocol::Packet> callbacks;
 bool connected = true, reject = false;
 Devices reject_device = UNKNOWN;
 Devices left_device = KEYSCANNER_DEFY_LEFT, right_device = KEYSCANNER_DEFY_RIGHT;
 Devices connectedSideDevice(Devices side) const {
   return side==KEYSCANNER_DEFY_LEFT ? left_device : side==KEYSCANNER_DEFY_RIGHT ? right_device : UNKNOWN;
 }
 using PacketFilter = bool (*)(const Packet &);
 PacketFilter filter = nullptr;
 void setPacketFilter(PacketFilter f) { filter=f; }
 std::vector<Packet> packets;
 bool is_host_connected() const { return connected; }
 bool sendPacket(Packet p) { if (filter && !filter(p)) return true; if (reject || (reject_device!=UNKNOWN && reject_device==p.header.device)) return false; packets.push_back(p); return true; }
};
extern MockCommunications Communications;
