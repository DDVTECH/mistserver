#include "box_avcC.h"

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

void Box_avcC::SetDimensions ( uint16_t Width, uint16_t Height ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( Height ),26);
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( Width ),24);
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
  SetDepth ( );
  SetFrameCount ( );
  SetResolution ( );
}
