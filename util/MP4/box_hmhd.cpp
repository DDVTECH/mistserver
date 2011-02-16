#include "box.cpp"

class Box_hmhd {
  public:
    Box_hmhd( );
    ~Box_hmhd();
    Box * GetBox();
    void SetMaxPDUSize( uint16_t Size = 0 );
    void SetAvgPDUSize( uint16_t Size = 0 );
    void SetMaxBitRate( uint32_t Rate = 0 );
    void SetAvgBitRate( uint32_t Rate = 0 );
  private:
    Box * Container;
    void SetReserved( );
    void SetDefaults( );
};//Box_ftyp Class

Box_hmhd::Box_hmhd( ) {
  Container = new Box( 0x686D6864 );
  SetDefaults();
  SetReserved();
}

Box_hmhd::~Box_hmhd() {
  delete Container;
}

Box * Box_hmhd::GetBox() {
  return Container;
}

void Box_hmhd::SetMaxPDUSize( uint16_t Size ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(Size),4);
}

void Box_hmhd::SetAvgPDUSize( uint16_t Size ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(Size),6);
}

void Box_hmhd::SetMaxBitRate( uint32_t Rate ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Rate),8);
}

void Box_hmhd::SetAvgBitRate( uint32_t Rate ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Rate),12);
}

void Box_hmhd::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),16);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
}
void Box_hmhd::SetDefaults( ) {
  SetAvgBitRate( );
  SetMaxBitRate( );
  SetAvgPDUSize( );
  SetMaxPDUSize( );
}
