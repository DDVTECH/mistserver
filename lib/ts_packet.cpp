#include "ts_packet.h"

TS_Packet::TS_Packet() {
  Free = 187;
  Buffer[0] = 0x47;
  for( int i = 1; i < 188; i++ ) {
    Buffer[i] = 0x00;
  }
}
