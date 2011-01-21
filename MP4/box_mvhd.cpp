#include "box_mvhd.h"

Box_mvhd::Box_mvhd( ) {
  Container = new Box( 0x6D766864 );
}

Box_mvhd::~Box_mvhd() {
  delete Container;
}

Box * Box_mvhd::GetBox() {
  return Container;
}

void Box_mvhd::SetCreationTime( uint32_t TimeStamp ) {
  uint32_t CreationTime;
  if(!TimeStamp) {
    CreationTime = time(NULL) + SECONDS_DIFFERENCE;
  } else {
    CreationTime = TimeStamp;
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(CreationTime),4);
}

void Box_mvhd::SetModificationTime( uint32_t TimeStamp ) {
  uint32_t ModificationTime;
  if(!TimeStamp) {
    ModificationTime = time(NULL) + SECONDS_DIFFERENCE;
  } else {
    ModificationTime = TimeStamp;
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(ModificationTime),8);
}

void Box_mvhd::SetTimeScale( uint32_t TimeUnits = 1 );

void Box_mvhd::SetDurationTime( uint32_t TimeUnits ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(TimeUnits),16);
}

void Box_mvhd::SetRate( uint32_t Rate = 0x00010000 );
void Box_mvhd::SetVolume( uint16_t Volume = 0x0100 );

void Box_mvhd::SetNextTrackID( uint32_t TrackID ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(TrackID),12);
}

void Box_mvhd::SetReserved() {
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

void Box_mvhd::SetDefaults() {
  SetHeight();
  SetWidth();
  SetCreationTime();
  SetModificationTime();
  SetDurationTime();
  SetFlags();
  SetVersion();
  SetTrackID();
}

