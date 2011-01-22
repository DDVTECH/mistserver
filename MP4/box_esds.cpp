#include "box_mp4a.h"

Box_mp4a::Box_mp4a( ) {
  Container = new Box( 0x6D703461 );
  SetReserved();
  SetDefaults();
}

Box_mp4a::~Box_mp4a() {
  delete Container;
}

Box * Box_mp4a::GetBox() {
  return Container;
}

void Box_mp4a::SetDataReferenceIndex( uint16_t DataReferenceIndex ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( DataReferenceIndex ),6);
}

void Box_mp4a::SetChannelCount( uint16_t Count ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( Count ),16);
}

void Box_mp4a::SetSampleSize( uint16_t Size ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( Size ),18);
}

void Box_mp4a::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8( 0 ),20);
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8( 0 ),4);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8( 0 ));
}

void Box_mp4a::SetDefaults( ) {
  SetSampleSize( );
  SetChannelCount( );
  SetDataReferenceIndex( );
}
