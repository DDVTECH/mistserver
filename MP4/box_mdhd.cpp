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
}
