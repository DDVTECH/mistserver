#include "box.h"

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

