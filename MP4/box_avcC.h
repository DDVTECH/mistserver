#include "box.h"
#include <string>

class Box_h264 {
  public:
    Box_h264( );
    ~Box_h264();
    Box * GetBox();
    void SetReserved( );
    void SetDataReferenceIndex( uint16_t DataReferenceIndex );
    void SetDimensions ( uint16_t Width, uint16_t Height );
    void SetResolution ( uint32_t Horizontal = 0x00480000, uint32_t Vertical = 0x00480000 );
    void SetFrameCount ( uint16_t FrameCount = 1 );
    void SetCompressorName ( std::string CompressorName );
    void SetDepth ( uint16_t Depth = 0x0018 );
    void SetDefaults( );
  private:
    Box * Container;
};//Box_ftyp Class

