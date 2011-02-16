#include "box.cpp"
#include <string>
#include <vector>

class Box_stco {
  public:
    Box_stco( );
    ~Box_stco();
    Box * GetBox();
    void AddOffset( uint32_t DataOffset, uint32_t Offset = 0 );
    void SetOffsets( std::vector<uint32_t> NewOffsets );
    void WriteContent( );
  private:
    Box * Container;
    
    void SetReserved( );
    std::vector<uint32_t> Offsets;
};//Box_ftyp Class

Box_stco::Box_stco( ) {
  Container = new Box( 0x7374636F );
  SetReserved();
}

Box_stco::~Box_stco() {
  delete Container;
}

Box * Box_stco::GetBox() {
  return Container;
}

void Box_stco::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
}

void Box_stco::AddOffset( uint32_t DataOffset, uint32_t Offset ) {
  if(Offset >= Offsets.size()) {
    Offsets.resize(Offset+1);
  }
  Offsets[Offset] = DataOffset;
}


void Box_stco::WriteContent( ) {
  Container->ResetPayload();
  SetReserved( );
  if(!Offsets.empty()) {
    for(int32_t i = Offsets.size() -1; i >= 0; i--) {
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Offsets[i]),(i*4)+8);
    }
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Offsets.size()),4);
}

void Box_stco::SetOffsets( std::vector<uint32_t> NewOffsets ) {
  Offsets = NewOffsets;
}
