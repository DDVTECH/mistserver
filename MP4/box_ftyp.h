#include "box.h"

class Box_ftyp {
  public:
    Box_ftyp( );
    ~Box_ftyp();
    Box * GetBox();
    void SetMajorBrand( uint32_t MajorBrand = 0x66347620 );
    void SetMinorBrand( uint32_t MinorBrand = 0x1 );
  private:
    void SetDefaults( );
    Box * Container;
};//Box_ftyp Class

