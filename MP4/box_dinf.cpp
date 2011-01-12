#include "box_dinf.h"

Box_dinf::Box_dinf( ) {
  Container = new Box( 0x64696E66 );
}

Box_dinf::~Box_dinf() {
  delete Container;
}

Box * Box_dinf::GetBox() {
  return Container;
}

void Box_dinf::AddContent( Box * newcontent, uint32_t offset ) {
  if(Content) {
    delete Content;
    Content = NULL;
  }
  Content = newcontent;
  WriteContent();
}

void Box_dinf::WriteContent( ) {
  Container->ResetPayload( );
  std::string serializedbox = "";
  serializedbox.append((char*)Content->GetBoxedData(),Content->GetBoxedDataSize());
  Container->SetPayload((uint32_t)serializedbox.size(),(uint8_t*)serializedbox.c_str());
}
