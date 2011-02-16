#include "box.cpp"

class Box_trex {
  public:
    Box_trex( );
    ~Box_trex();
    Box * GetBox();
    void SetTrackID( uint32_t Id = 0 );
    void SetSampleDescriptionIndex( uint32_t Index = 0 );
    void SetSampleDuration( uint32_t Duration = 0 );
    void SetSampleSize( uint32_t Size = 0 );
  private:
    void SetReserved( );
    void SetDefaults( );
    Box * Container;
};//Box_ftyp Class

Box_trex::Box_trex( ) {
  Container = new Box( 0x74726578 );
  SetReserved( );
  SetDefaults( );
}

Box_trex::~Box_trex() {
  delete Container;
}

Box * Box_trex::GetBox() {
  return Container;
}

void Box_trex::SetDefaults( ) {
  SetTrackID( );
  SetSampleDescriptionIndex( );
  SetSampleDuration( );
  SetSampleSize( );
}

void Box_trex::SetReserved( ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(1),22);
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(0),20);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
}

void Box_trex::SetTrackID( uint32_t Id ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Id),4);
}

void Box_trex::SetSampleDescriptionIndex( uint32_t Index ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Index),8);
}

void Box_trex::SetSampleDuration( uint32_t Duration ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Duration),12);
}

void Box_trex::SetSampleSize( uint32_t Size ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Size),16);
}
