#include "box_abst.h"

Box_abst::Box_abst( ) {
  Container = new Box( 0x61627374 );
}

Box_abst::~Box_abst() {
  delete Container;
}

Box * Box_abst::GetBox() {
  return Container;
}
