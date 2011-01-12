#include "box_vmhd.h"

Box_vmhd::Box_vmhd( ) {
  Container = new Box( 0x766D6864 );
  SetDefaults();
  SetReserved();
}

Box_vmhd::~Box_vmhd() {
  delete Container;
}

Box * Box_vmhd::GetBox() {
  return Container;
}

void Box_vmhd::SetGraphicsMode( uint16_t GraphicsMode ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(GraphicsMode),8);
}
void Box_vmhd::SetOpColor( uint16_t Red, uint16_t Green, uint16_t Blue ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(Blue),14);
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(Green),12);
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(Red),10);
}

void Box_vmhd::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(1));
}
void Box_vmhd::SetDefaults( ) {
  SetOpColor();
  SetGraphicsMode();
}
