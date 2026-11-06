// Actual SPI enqueue body + actual release FIFO, without any hardware calls.
#include <cassert>
#include <cstring>
#include "Communications_protocol.h"
#include "Fifo_buffer.h"
using namespace Communications_protocol;
struct FakeSpi { Fifo_buffer *tx_fifo; };
class SpiPort {
 public:
  FakeSpi *spi_slave;
  explicit SpiPort(FakeSpi *spi):spi_slave(spi) {}
  bool sendPacket(Packet &packet);
};
#include "actual_spi_send.inc"
int main() {
  Fifo_buffer fifo(sizeof(Packet));FakeSpi spi{&fifo};SpiPort port(&spi),absent(nullptr);
  Packet p{};assert(!absent.sendPacket(p));
  const unsigned capacity=INTERNAL_BUFFER_SIZE/sizeof(Packet);
  for(unsigned i=0;i<capacity;++i) {p.data[0]=i;assert(port.sendPacket(p));}
  p.data[0]=238;assert(!port.sendPacket(p)); // Cannot report success on overflow.
  Packet received{};assert(fifo.get(&received)==32 && received.data[0]==0);
  assert(port.sendPacket(p));
  for(unsigned i=1;i<capacity;++i) assert(fifo.get(&received)==32 && received.data[0]==i);
  assert(fifo.get(&received)==32 && received.data[0]==238);
  assert(fifo.is_empty());
}
