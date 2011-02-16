#include "box.cpp"
#include <vector>

struct trun_sampleinformationstructure {
  uint32_t SampleDuration;
  uint32_t SampleSize;
};

class Box_trun {
  public:
    Box_trun( );
    ~Box_trun();
    Box * GetBox();
    void SetDataOffset( uint32_t Offset = 0 );
    void AddSampleInformation( uint32_t SampleDuration = 0, uint32_t SampleSize = 0, uint32_t Offset = 0 );
    void WriteContent( );
  private:
    void SetDefaults( );
    bool setSampleDuration;
    bool setSampleSize;
    uint32_t curDataOffset;
    std::vector<trun_sampleinformationstructure> SampleInfo;
    Box * Container;
};//Box_ftyp Class

Box_trun::Box_trun( ) {
  Container = new Box( 0x74666864 );
  SetDefaults( );
}

Box_trun::~Box_trun() {
  delete Container;
}

Box * Box_trun::GetBox() {
  return Container;
}

void Box_trun::SetDataOffset( uint32_t Offset ) {
  curDataOffset = Offset;
}

void Box_trun::WriteContent( ) {
  uint32_t curoffset;
  uint32_t flags = 0 & ( curDataOffset ? 0x1 : 0 ) & ( setSampleDuration ? 0x100 : 0 ) & ( setSampleSize ? 0x200 : 0 );
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(flags));
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(SampleInfo.size()),4);
  curoffset = 8;
  if( curDataOffset ) {
    Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(curDataOffset),curoffset);
    curoffset += 4;
  }
  for( uint32_t i = 0; i < SampleInfo.size(); i++ ) {
    if( setSampleDuration ) {
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(SampleInfo[i].SampleDuration),curoffset);
      curoffset += 4;
    }
    if( setSampleSize ) {
      Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(SampleInfo[i].SampleSize),curoffset);
      curoffset += 4;
    }
  }
}

void Box_trun::SetDefaults( ) {
  setSampleDuration = false;
  setSampleSize = false;
}
