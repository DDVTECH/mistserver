#include "box.cpp"

class Box_nmhd {
  public:
    Box_nmhd( );
    ~Box_nmhd();
    Box * GetBox();
  private:
    Box * Container;
    void SetReserved( );
};//Box_ftyp Class

Box_nmhd::Box_nmhd( ) {
  Container = new Box( 0x6E6D6864 );
  SetReserved();
}

Box_nmhd::~Box_nmhd() {
  delete Container;
}

Box * Box_nmhd::GetBox() {
  return Container;
}

void Box_nmhd::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
}
