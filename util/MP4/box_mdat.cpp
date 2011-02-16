#include "box.cpp"
#include <vector>
#include <string>

class Box_mdat {
  public:
    Box_mdat();
    ~Box_mdat();
    Box * GetBox();
    void SetContent( uint8_t * NewData, uint32_t DataLength , uint32_t offset = 0 );
  private:
    Box * Container;
};//Box_ftyp Class

Box_mdat::Box_mdat( ) {
  Container = new Box( 0x6D646174 );
}

Box_mdat::~Box_mdat() {
  delete Container;
}

Box * Box_mdat::GetBox() {
  return Container;
}

void Box_mdat::SetContent( uint8_t * NewData, uint32_t DataLength , uint32_t Offset ) {
  Container->SetPayload(DataLength,NewData,Offset);
}
