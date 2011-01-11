#include "box_stsc.h"

Box_stsc::Box_stsc( ) {
  Container = new Box( 0x73747363 );
  SetReserved();
}

Box_stsc::~Box_stsc() {
  delete Container;
}

Box * Box_stsc::GetBox() {
  return Container;
}

void Box_stsc::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
}

void Box_stsc::AddEntry( uint32_t FirstChunk, uint32_t SamplesPerChunk, uint32_t SampleDescIndex, uint32_t Offset = 0 ) {
  if(Offset >= Entries.size()) {
    Entries.resize(Offset+1);
  }
  Entries[Offset].FirstChunk = FirstChunk;
  Entries[Offset].SamplesPerChunk = SamplesPerChunk;
  Entries[Offset].SampleDescIndex = SampleDescIndex;
  WriteEntries( );
}


void Box_stsc::WriteEntries( ) {
  Container->ResetPayload();
  SetReserved( );
  if(!Offsets.empty()) {
    for(int32_t i = Offsets.size() -1; i >= 0; i--) {
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries[i].SampleDescIndex),(i*12)+16);
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries[i].SamplesPerChunk),(i*12)+12);
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries[i].FirstChunk),(i*12)+8);
    }
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Offsets.size()),4);
}
