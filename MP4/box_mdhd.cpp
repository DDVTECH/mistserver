#include "box_mdhd.h"

Box_mdhd::Box_mdhd( ) {
  Container = new Box( 0x6D646864 );
}

Box_mdhd::~Box_mdhd() {
  delete Container;
}

Box * Box_mdhd::GetBox() {
  return Container;
}

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

void Box_mdhd::SetTimeScale( uint32_t TimeUnits ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(TimeUnits),12);
}

void Box_mdhd::SetDurationTime( uint32_t TimeUnits ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(TimeUnits),16);
}

void Box_mdhd::SetLanguage( uint8_t Firstchar, uint8_t Secondchar, uint8_t Thirdchar ) {
  uint8_t FirstByte = 0;
  uint8_t SecondByte = 0;
  Firstchar -= 0x60;
  Secondchar -= 0x60;
  Thirdchar -= 0x60;
  FirstByte += (Firstchar << 2);
  FirstByte += (Secondchar >> 3);
  SecondByte += (Secondchar << 5);
  SecondByte += Thirdchar;

  Container->SetPayload((uint32_t)1,&SecondByte,21);
  Container->SetPayload((uint32_t)1,&FirstByte,20);
}

void Box_mdhd::SetReserved() {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(0),22);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
}

void Box_mdhd::SetDefaults() {
  SetLanguage();
  SetDurationTime();
  SetTimeScale();
  SetModificationTime();
  SetCreationTime();
}
