#include "box_afra.h"

Box_afra::Box_afra( ) {
  Container = new Box( 0x61667261 );
  SetReserved( );
  SetDefaults( );
}

Box_afra::~Box_afra() {
  delete Container;
}

Box * Box_afra::GetBox() {
  return Container;
}

void Box_afra::SetDefaults( ) {
  SetTimeScale( );
}

void Box_afra::SetReserved( ) {
  uint8_t * temp = new uint8_t[1];
  temp[0] = 0;
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),1);
  Container->SetPayload((uint32_t)1,temp);
}

void Box_afra::SetTimeScale( uint32_t Scale ) {
  CurrentTimeScale = Scale;
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Scale),5);
}

void Box_afra::WriteContent( ) {
  Container->ResetPayload();
  SetReserved( );
  if(!Entries.empty()) {
    for(int32_t i = Entries.size() -1; i >= 0; i--) {
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries[i].SampleDelta),(i*12)+21);
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries[i].SampleCount),(i*12)+17);
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),(i*12)+13);
    }
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries.size()),9);
}
