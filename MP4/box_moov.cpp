#include "box_moov.h"

Box_moov::Box_moov( ) {
  Container = new Box( 0x6D6F6F76 );
}

Box_moov::~Box_moov() {
  delete Container;
}

Box * Box_moov::GetBox() {
  return Container;
}

void Box_moov::AddContent( Box * newcontent, uint32_t offset ) {
  if( offset >= Content.size() ) {
    Content.resize(offset+1);
  }
  if( Content[offset] ) {
    delete Content[offset];
  }
  Content[offset] = newcontent;
  WriteContent();
}

void Box_moov::WriteContent( ) {
  Container->ResetPayload( );
  Box * current;
  std::string serializedbox = "";
  for( uint32_t i = 0; i < Content.size(); i++ ) {
    current=Content[i];
    if( current ) {
      serializedbox.append((char*)current->GetBoxedData(),current->GetBoxedDataSize());
    }
  }
  Container->SetPayload((uint32_t)serializedbox.size(),(uint8_t*)serializedbox.c_str());
}
