#include "box.cpp"
#include <string>

class Box_avcC {
  public:
    Box_avcC( );
    ~Box_avcC();
    Box * GetBox();
    void SetDataReferenceIndex( uint16_t DataReferenceIndex = 1 );
    void SetWidth( uint16_t Width = 0 );
    void SetHeight( uint16_t Height = 0 );
    void SetResolution ( uint32_t Horizontal = 0x00480000, uint32_t Vertical = 0x00480000 );
    void SetFrameCount ( uint16_t FrameCount = 1 );
    void SetCompressorName ( std::string CompressorName = "");
    void SetDepth ( uint16_t Depth = 0x0018 );
  private:
    Box * Container;
    
    void SetReserved( );
    void SetDefaults( );
};//Box_ftyp Class

Box_avcC::Box_avcC( ) {
  Container = new Box( 0x61766343 );
  SetReserved();
  SetDefaults();
}

Box_avcC::~Box_avcC() {
  delete Container;
}

Box * Box_avcC::GetBox() {
  return Container;
}

void Box_avcC::SetDataReferenceIndex( uint16_t DataReferenceIndex ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( DataReferenceIndex ),6);
}

void Box_avcC::SetWidth( uint16_t Width ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( Width ),24);
}

void Box_avcC::SetHeight( uint16_t Height ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( Height ),26);
}

void Box_avcC::SetResolution ( uint32_t Horizontal, uint32_t Vertical ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8( Vertical ),32);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8( Horizontal ),28);
}

void Box_avcC::SetFrameCount ( uint16_t FrameCount ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( FrameCount ),40);
}

void Box_avcC::SetCompressorName ( std::string CompressorName ) {
  uint8_t * Printable = new uint8_t[1];
  Printable[0] = std::min( (unsigned int)31, CompressorName.size() );
  Container->SetPayload((uint32_t)Printable[0],(uint8_t*)CompressorName.c_str(),43);
  Container->SetPayload((uint32_t)1, Printable ,42);
}

void Box_avcC::SetDepth ( uint16_t Depth ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( Depth ),74);
}

void Box_avcC::SetReserved( ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( (uint16_t)-1 ),76);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),36);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),20);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),16);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),12);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),8);
  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(0),4);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
}

void Box_avcC::SetDefaults( ) {
  SetWidth( );
  SetHeight( );
  SetDepth ( );
  SetFrameCount ( );
  SetResolution ( );
}
