#include "box_ftyp.h"

Box_ftyp::Box_ftyp( ) {
  Container = new Box( 0x66747970 );
  SetDefaults( );
}

Box_ftyp::~Box_ftyp() {
  delete Container;
}

Box * Box_ftyp::GetBox() {
  return Container;
}

void Box_ftyp::SetMajorBrand( uint32_t MajorBrand ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(MajorBrand));
}

void Box_ftyp::SetMinorBrand( uint32_t MinorBrand ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(MinorBrand),4);
}

void Box_ftyp::SetDefaults( ) {
  SetMinorBrand( );
  SetMajorBrand( );
}
