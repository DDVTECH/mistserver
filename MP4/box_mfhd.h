#include "box.h"

class Box_mfhd {
  public:
    Box_mfhd( );
    ~Box_mfhd();
    Box * GetBox();
    void SetSequenceNumber( uint32_t SequenceNumber = 1 );
  private:
    void SetDefaults( );
    void SetReserved( );
    Box * Container;
};//Box_ftyp Class

