#include "box_stsd.h"

Box_stsd::Box_stsd( ) {
  Container = new Box( 0x73747364 );
  SetDefaults();
}

Box_stsd::~Box_stsd() {
  delete Container;
}

Box * Box_stsd::GetBox() {
  return Container;
}

void Box_stsd::AddContent( Box * newcontent, uint32_t offset ) {
  if( offset >= Content.size() ) {
    Content.resize(offset+1);
  }
  if( Content[offset] ) {
    delete Content[offset];
  }
  Content[offset] = newcontent;
  WriteContent();
}

void Box_stsd::SetDefaults( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8( 1 ),4);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8( 0 ),0);
}

void Box_stsd::WriteContent( ) {
  Container->ResetPayload( );
  SetDefaults( );
  Box * current;
  std::string serializedbox = "";
  for( uint32_t i = 0; i < Content.size(); i++ ) {
    current=Content[i];
    if( current ) {
      serializedbox.append((char*)current->GetBoxedData(),current->GetBoxedDataSize());
    }
  }
  Container->SetPayload((uint32_t)serializedbox.size(),(uint8_t*)serializedbox.c_str(),8);
}
