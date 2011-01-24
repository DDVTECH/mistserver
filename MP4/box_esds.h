#include "box.h"
#include <string>

class Box_esds {
  public:
    Box_esds( );
    ~Box_esds();
    Box * GetBox();
    void SetDataReferenceIndex( uint16_t DataReferenceIndex = 1);
    void SetChannelCount( uint16_t Count = 2 );
    void SetSampleSize( uint16_t Size = 16 );
  private:
    Box * Container;

    void SetReserved( );
    void SetDefaults( );
};//Box_ftyp Class

