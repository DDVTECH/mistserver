#include "box.h"

class Box_hmhd {
  public:
    Box_hmhd( );
    ~Box_hmhd();
    Box * GetBox();
    void SetMaxPDUSize( uint16_t Size = 0 );
    void SetAvgPDUSize( uint16_t Size = 0 );
    void SetMaxBitRate( uint32_t Rate = 0 );
    void SetAvgBitRate( uint32_t Rate = 0 );
  private:
    Box * Container;
    void SetReserved( );
    void SetDefaults( );
};//Box_ftyp Class

