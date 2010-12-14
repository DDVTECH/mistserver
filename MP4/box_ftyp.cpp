#include "box_ftyp.h"

Box_ftyp::Box_ftyp( uint32_t MajorBrand, uint32_t MinorBrand ) {
  Container = new Box( 0x66747970 );
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(MajorBrand));
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(MinorBrand),4);
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
