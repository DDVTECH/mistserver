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

/*
void Box_mdhd::SetCreationTime( uint32_t TimeStamp ) {
  uint32_t CreationTime;
  if(!TimeStamp) {
    CreationTime = time(NULL) + SECONDS_DIFFERENCE;
  } else {
    CreationTime = TimeStamp;
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(CreationTime),4);
}

void Box_mdhd::SetModificationTime( uint32_t TimeStamp ) {
  uint32_t ModificationTime;
  if(!TimeStamp) {
    ModificationTime = time(NULL) + SECONDS_DIFFERENCE;
  } else {
    ModificationTime = TimeStamp;
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(ModificationTime),8);
}

void Box_mdhd::SetDurationTime( uint32_t TimeUnits ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(TimeUnits),16);
}

void Box_mdhd::SetReserved() {
}

void Box_mdhd::SetDefaults() {
}
*/
