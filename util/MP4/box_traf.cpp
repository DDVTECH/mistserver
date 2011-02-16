#include "box.cpp"
#include <vector>
#include <string>

class Box_traf {
  public:
    Box_traf();
    ~Box_traf();
    Box * GetBox();
    void AddContent( Box * newcontent, uint32_t offset = 0 );
    void WriteContent( );
  private:
    Box * Container;
    
    std::vector<Box *> Content;
};//Box_ftyp Class

Box_traf::Box_traf( ) {
  Container = new Box( 0x74726166 );
}

Box_traf::~Box_traf() {
  delete Container;
}

Box * Box_traf::GetBox() {
  return Container;
}

void Box_traf::AddContent( Box * newcontent, uint32_t offset ) {
  if( offset >= Content.size() ) {
    Content.resize(offset+1);
  }
  if( Content[offset] ) {
    delete Content[offset];
  }
  Content[offset] = newcontent;
}

void Box_traf::WriteContent( ) {
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
