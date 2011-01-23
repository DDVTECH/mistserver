#include "box.h"
#include <string>

class Box_avcC {
  public:
    Box_avcC( );
    ~Box_avcC();
    Box * GetBox();
    void SetDataReferenceIndex( uint16_t DataReferenceIndex = 0 );
    void SetWidth( uint16_t Width = 0 );
    void SetHeight( uint16_t Height = 0 );
    void SetResolution ( uint32_t Horizontal = 0x00480000, uint32_t Vertical = 0x00480000 );
    void SetFrameCount ( uint16_t FrameCount = 1 );
    void SetCompressorName ( std::string CompressorName = "");
    void SetDepth ( uint16_t Depth = 0x0018 );
  private:
    Box * Container;

    void SetReserved( );
    void SetDefaults( );
};//Box_ftyp Class

