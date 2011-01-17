#include "box_tkhd.h"

Box_tkhd::Box_tkhd( ) {
  Container = new Box( 0x6D646864 );
}

Box_tkhd::~Box_tkhd() {
  delete Container;
}

Box * Box_tkhd::GetBox() {
  return Container;
}

void Box_tkhd::SetCreationTime( uint32_t TimeStamp ) {
  uint32_t CreationTime;
  if(!TimeStamp) {
    CreationTime = time(NULL) + SECONDS_DIFFERENCE;
  } else {
    CreationTime = TimeStamp;
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(CreationTime),4);
}

void Box_tkhd::SetModificationTime( uint32_t TimeStamp ) {
  uint32_t ModificationTime;
  if(!TimeStamp) {
    ModificationTime = time(NULL) + SECONDS_DIFFERENCE;
  } else {
    ModificationTime = TimeStamp;
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(ModificationTime),8);
}

void Box_tkhd::SetDurationTime( uint32_t TimeUnits ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(TimeUnits),16);
}

void Box_tkhd::SetReserved() {
  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(0x40000000),68);
  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(0),64);
  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(0),60);
  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(0),56);
  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(0x10000),52);
  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(0),48);
  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(0),44);
  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(0),40);
  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(0x10000),36);
  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(0),34);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),28);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),24);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),20);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),12);
}

/*
void Box_tkhd::SetDefaults() {
}
*/
