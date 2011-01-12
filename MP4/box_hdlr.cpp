#include "box_hdlr.h"

Box_hdlr::Box_hdlr( ) {
  Container = new Box( 0x68646C72 );
  SetDefaults();
  SetReserved();
}

Box_hdlr::~Box_hdlr() {
  delete Container;
}

Box * Box_hdlr::GetBox() {
  return Container;
}

void Box_hdlr::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(1));
}

