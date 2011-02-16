#include "box.cpp"
#include <vector>
#include <string>

class Box_dinf {
  public:
    Box_dinf();
    ~Box_dinf();
    Box * GetBox();
    void AddContent( Box * newcontent );
    void WriteContent( );
  private:
    Box * Container;
    
    Box * Content;
};//Box_ftyp Class

Box_dinf::Box_dinf( ) {
  Container = new Box( 0x64696E66 );
}

Box_dinf::~Box_dinf() {
  delete Container;
}

Box * Box_dinf::GetBox() {
  return Container;
}

void Box_dinf::AddContent( Box * newcontent ) {
  if(Content) {
    delete Content;
    Content = NULL;
  }
  Content = newcontent;
}

void Box_dinf::WriteContent( ) {
  Container->ResetPayload( );
  std::string serializedbox = "";
  serializedbox.append((char*)Content->GetBoxedData(),Content->GetBoxedDataSize());
  Container->SetPayload((uint32_t)serializedbox.size(),(uint8_t*)serializedbox.c_str());
}
