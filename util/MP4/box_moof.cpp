#include "box.cpp"
#include <vector>
#include <string>

class Box_moof {
  public:
    Box_moof();
    ~Box_moof();
    Box * GetBox();
    void AddContent( Box * newcontent, uint32_t offset = 0 );
    void WriteContent( );
  private:
    Box * Container;
    
    std::vector<Box *> Content;
};//Box_ftyp Class

Box_moof::Box_moof( ) {
  Container = new Box( 0x6D6F6F66 );
}

Box_moof::~Box_moof() {
  delete Container;
}

Box * Box_moof::GetBox() {
  return Container;
}

void Box_moof::AddContent( Box * newcontent, uint32_t offset ) {
  if( offset >= Content.size() ) {
    Content.resize(offset+1);
  }
  if( Content[offset] ) {
    delete Content[offset];
  }
  Content[offset] = newcontent;
}

void Box_moof::WriteContent( ) {
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
