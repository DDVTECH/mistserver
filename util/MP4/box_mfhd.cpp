#include "box.cpp"

class Box_mfhd {
  public:
    Box_mfhd( );
    ~Box_mfhd();
    Box * GetBox();
    void SetSequenceNumber( uint32_t SequenceNumber = 1 );
  private:
    void SetDefaults( );
    void SetReserved( );
    Box * Container;
};//Box_ftyp Class

Box_mfhd::Box_mfhd( ) {
  Container = new Box( 0x6D666864 );
  SetDefaults( );
  SetReserved( );
}

Box_mfhd::~Box_mfhd() {
  delete Container;
}

Box * Box_mfhd::GetBox() {
  return Container;
}

void Box_mfhd::SetDefaults( ) {
  SetSequenceNumber( );
}

void Box_mfhd::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
}

void Box_mfhd::SetSequenceNumber( uint32_t SequenceNumber ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(SequenceNumber),4);
}
