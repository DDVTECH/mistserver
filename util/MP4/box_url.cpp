#include "box.cpp"

class Box_url {
  public:
    Box_url( );
    ~Box_url();
    Box * GetBox();
  private:
    Box * Container;
};//Box_ftyp Class

Box_url::Box_url( ) {
  Container = new Box( 0x75726C20 );
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(1));
}

Box_url::~Box_url() {
  delete Container;
}

Box * Box_url::GetBox() {
  return Container;
}
