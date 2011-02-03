#include "box_rtmp.h"

Box_rtmp::Box_rtmp( ) {
  Container = new Box( 0x72746D70 );
  SetReserved();
  SetDefaults();
}

Box_rtmp::~Box_rtmp() {
  delete Container;
}

Box * Box_rtmp::GetBox() {
  return Container;
}

void Box_rtmp::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(0));
}

void Box_rtmp::SetDefaults( ) {
  SetDataReferenceIndex( );
  SetHintTrackVersion( );
  SetHighestCompatibleVersion( );
  SetMaxPacketSize( );
}

void Box_rtmp::SetDataReferenceIndex( uint16_t NewIndex ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(NewIndex),6);
}
void Box_rtmp::SetHintTrackVersion( uint16_t NewVersion ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(NewVersion),8);
}
void Box_rtmp::SetHighestCompatibleVersion( uint16_t NewVersion ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(NewVersion),10);
}

void Box_rtmp::SetMaxPacketSize( uint16_t NewSize ) {
  Container->SetPayload((uint32_t)2,Box::uint16_to_uint8(NewSize),12);
}

void Box_rtmp::AddContent( Box * newcontent ) {
  if(Content) {
    delete Content;
    Content = NULL;
  }
  Content = newcontent;
}

void Box_rtmp::WriteContent( ) {
  Container->ResetPayload( );
  SetDefaults( );
  std::string serializedbox = "";
  serializedbox.append((char*)Content->GetBoxedData(),Content->GetBoxedDataSize());
  Container->SetPayload((uint32_t)serializedbox.size(),(uint8_t*)serializedbox.c_str(),14);
}
