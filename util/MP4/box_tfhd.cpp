#include "box.cpp"

class Box_tfhd {
  public:
    Box_tfhd( );
    ~Box_tfhd();
    Box * GetBox();
    void SetTrackID( uint32_t TrackID = 0 );
    void SetBaseDataOffset( uint32_t Offset = 0 );//write as uint64_t
    void SetSampleDescriptionIndex( uint32_t Index = 0 );
    void SetDefaultSampleDuration( uint32_t Duration = 0 );
    void SetDefaultSampleSize( uint32_t Size = 0 );
    void WriteContent( );
  private:
    void SetDefaults( );
    uint32_t curTrackID;
    uint32_t curBaseDataOffset;
    uint32_t curSampleDescriptionIndex;
    uint32_t curDefaultSampleDuration;
    uint32_t curDefaultSampleSize;
    Box * Container;
};//Box_ftyp Class

Box_tfhd::Box_tfhd( ) {
  Container = new Box( 0x74666864 );
  SetDefaults( );
}

Box_tfhd::~Box_tfhd() {
  delete Container;
}

Box * Box_tfhd::GetBox() {
  return Container;
}

void Box_tfhd::SetTrackID( uint32_t TrackID ) {
  curTrackID = TrackID;
}

void Box_tfhd::SetBaseDataOffset( uint32_t Offset ) {
  curBaseDataOffset = Offset;
}

void Box_tfhd::SetSampleDescriptionIndex( uint32_t Index ) {
  curSampleDescriptionIndex = Index;
}

void Box_tfhd::SetDefaultSampleDuration( uint32_t Duration ) {
  curDefaultSampleDuration = Duration;
}

void Box_tfhd::SetDefaultSampleSize( uint32_t Size ) {
  curDefaultSampleSize = Size;
}

void Box_tfhd::WriteContent( ) {
  uint32_t curoffset;
  uint32_t flags = 0 & ( curBaseDataOffset ? 0x1 : 0 ) & ( curSampleDescriptionIndex ? 0x2 : 0 ) & ( curDefaultSampleDuration ? 0x8 : 0 ) & ( curDefaultSampleSize ? 0x10 : 0 );
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(flags));
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(curTrackID),4);
  curoffset = 8;
  if( curBaseDataOffset ) {
    Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),curoffset);
    Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(curBaseDataOffset),curoffset+4);
    curoffset += 8;
  }
  if( curSampleDescriptionIndex ) {
    Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(curSampleDescriptionIndex),curoffset);
    curoffset += 8;
  }
  if( curDefaultSampleDuration ) {
    Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(curDefaultSampleDuration),curoffset);
    curoffset += 8;
  }
  if( curDefaultSampleSize ) {
    Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(curDefaultSampleSize),curoffset);
    curoffset += 8;
  }
}

void Box_tfhd::SetDefaults( ) {
  SetTrackID( );
  SetBaseDataOffset( );
  SetSampleDescriptionIndex( );
  SetDefaultSampleDuration( );
  SetDefaultSampleSize( );
}
