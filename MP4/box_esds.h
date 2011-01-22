#include "box.h"
#include <string>

class Box_mp4a {
  public:
    Box_mp4a( );
    ~Box_mp4a();
    Box * GetBox();
    void SetDataReferenceIndex( uint16_t DataReferenceIndex = 0);
    void SetChannelCount( uint16_t Count = 2 );
    void SetSampleSize( uint16_t Size = 16 );
  private:
    Box * Container;

    void SetReserved( );
    void SetDefaults( );
};//Box_ftyp Class

