#include "box_mdat.h"

Box_mdat::Box_mdat( ) {
  Container = new Box( 0x6D646174 );
}

Box_mdat::~Box_mdat() {
  delete Container;
}

Box * Box_mdat::GetBox() {
  return Container;
}

void Box_mdat::SetContent( uint8_t * NewData, uint32_t DataLength , uint32_t Offset ) {
  Container->SetPayload(DataLength,NewData,Offset);
}
