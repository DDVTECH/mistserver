#include "box_amhp.h"

Box_amhp::Box_amhp( ) {
  Container = new Box( 0x616D6870 );
  SetReserved();
}

Box_amhp::~Box_amhp() {
  delete Container;
}

Box * Box_amhp::GetBox() {
  return Container;
}

void Box_amhp::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
}

void Box_stts::AddEntry( uint32_t SampleCount, uint32_t SampleDelta, uint32_t Offset ) {
  if(Offset >= Entries.size()) {
    Entries.resize(Offset+1);
  }
  Entries[Offset].SampleCount = SampleCount;
  Entries[Offset].SampleDelta = SampleDelta;
}


void Box_stts::WriteContent( ) {
  Container->ResetPayload();
  SetReserved( );
  if(!Entries.empty()) {
    for(int32_t i = Entries.size() -1; i >= 0; i--) {
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries[i].SampleDelta),(i*8)+12);
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries[i].SampleCount),(i*8)+8);
    }
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries.size()),4);
}
