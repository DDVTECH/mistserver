#include "box.h"

class Box_ftyp {
  public:
    Box_ftyp( uint32_t MajorBrand = 0x66347620, uint32_t MinorBrand = 0x1);
    ~Box_ftyp();
    Box * GetBox();
    void SetMajorBrand( uint32_t MajorBrand );
    void SetMinorBrand( uint32_t MinorBrand );
  private:
    Box * Container;
};//Box_ftyp Class

