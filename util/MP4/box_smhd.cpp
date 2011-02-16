#include "box.cpp"

class Box_smhd {
  public:
    Box_smhd( );
    ~Box_smhd();
    Box * GetBox();
  private:
    Box * Container;
    void SetReserved( );
};//Box_ftyp Class

Box_smhd::Box_smhd( ) {
  Container = new Box( 0x736D6864 );
  SetReserved();
}

Box_smhd::~Box_smhd() {
  delete Container;
}

Box * Box_smhd::GetBox() {
  return Container;
}

void Box_smhd::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),4);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
}
