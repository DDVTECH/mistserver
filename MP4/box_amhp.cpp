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

void Box_amhp::AddEntry( uint8_t HintTrackMode, uint8_t Settings, uint8_t TrailerDefaultSize, uint32_t Offset ) {
  if(Offset >= Entries.size()) {
    Entries.resize(Offset+1);
  }
  Entries[Offset].HintTrackMode = HintTrackMode;
  Entries[Offset].Settings = Settings;
  Entries[Offset].TrailerDefaultSize = TrailerDefaultSize;
}


void Box_amhp::WriteContent( ) {
  Container->ResetPayload();
  SetReserved( );
  if(!Entries.empty()) {
    for(int32_t i = Entries.size() -1; i >= 0; i--) {
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries[i].TrailerDefaultSize),(i*12)+16);
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries[i].Settings),(i*12)+12);
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries[i].HintTrackMode),(i*12)+8);
    }
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries.size()),4);
}
