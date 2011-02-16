#include "box.cpp"
#include <string>

class Box_esds {
  public:
    Box_esds( );
    ~Box_esds();
    Box * GetBox();
    void SetDataReferenceIndex( uint16_t DataReferenceIndex = 1);
    void SetChannelCount( uint16_t Count = 2 );
    void SetSampleSize( uint16_t Size = 16 );
  private:
    Box * Container;
    
    void SetReserved( );
    void SetDefaults( );
};//Box_ftyp Class

Box_esds::Box_esds( ) {
  Container = new Box( 0x65736473 );
  SetReserved();
  SetDefaults();
}

Box_esds::~Box_esds() {
  delete Container;
}

Box * Box_esds::GetBox() {
  return Container;
}

void Box_esds::SetDataReferenceIndex( uint16_t DataReferenceIndex ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( DataReferenceIndex ),6);
}

void Box_esds::SetChannelCount( uint16_t Count ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( Count ),16);
}

void Box_esds::SetSampleSize( uint16_t Size ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( Size ),18);
}

void Box_esds::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8( 0 ),20);
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( 0 ),4);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8( 0 ));
}

void Box_esds::SetDefaults( ) {
  SetSampleSize( );
  SetChannelCount( );
  SetDataReferenceIndex( );
}
