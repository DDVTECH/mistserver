#include "box_dref.h"

Box_dref::Box_dref( ) {
  Container = new Box( 0x64726566 );
  SetReserved( );
}

Box_dref::~Box_dref() {
  delete Container;
}

Box * Box_dref::GetBox() {
  return Container;
}

void Box_dref::AddContent( Box * newcontent, uint32_t offset ) {
  if( offset >= Content.size() ) {
    Content.resize(offset+1);
  }
  if( Content[offset] ) {
    delete Content[offset];
  }
  Content[offset] = newcontent;
}

void Box_dref::WriteContent( ) {
  Container->ResetPayload( );
  Box * current;
  std::string serializedbox = "";
  for( uint32_t i = 0; i < Content.size(); i++ ) {
    current=Content[i];
    if( current ) {
      serializedbox.append((char*)current->GetBoxedData(),current->GetBoxedDataSize());
    }
  }
  Container->SetPayload((uint32_t)serializedbox.size(),(uint8_t*)serializedbox.c_str(),4);
  SetReserved( );
}

void Box_dref::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
}
