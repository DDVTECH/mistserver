#include "box.cpp"
#include <vector>

struct afra_record {
  uint32_t Time;
  uint32_t Offset;
};//afra_record

class Box_afra {
  public:
    Box_afra( );
    ~Box_afra();
    Box * GetBox();
    void SetTimeScale( uint32_t Scale = 1 );
    void AddEntry( uint32_t Time = 0, uint32_t SampleOffset = 0, uint32_t Offset = 0 );
    void WriteContent( );
  private:
    void SetReserved( );
    void SetDefaults( );
    
    Box * Container;
    uint32_t CurrentTimeScale;
    std::vector<afra_record> Entries;
};//Box_ftyp Class

Box_afra::Box_afra( ) {
  Container = new Box( 0x61667261 );
  SetReserved( );
  SetDefaults( );
}

Box_afra::~Box_afra() {
  delete Container;
}

Box * Box_afra::GetBox() {
  return Container;
}

void Box_afra::SetDefaults( ) {
  SetTimeScale( );
}

void Box_afra::SetReserved( ) {
  uint8_t * temp = new uint8_t[1];
  temp[0] = 0;
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),1);
  Container->SetPayload((uint32_t)1,temp);
}

void Box_afra::SetTimeScale( uint32_t Scale ) {
  CurrentTimeScale = Scale;
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Scale),5);
}

void Box_afra::AddEntry( uint32_t Time, uint32_t SampleOffset, uint32_t Offset ) {
  if(Offset >= Entries.size()) {
    Entries.resize(Offset+1);
  }
  Entries[Offset].Time = Time;
  Entries[Offset].Offset = SampleOffset;
}

void Box_afra::WriteContent( ) {
  Container->ResetPayload();
  SetReserved( );
  if(!Entries.empty()) {
    for(int32_t i = Entries.size() -1; i >= 0; i--) {
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries[i].Offset),(i*12)+21);
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries[i].Time),(i*12)+17);
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),(i*12)+13);
    }
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Entries.size()),9);
}
