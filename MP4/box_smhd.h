#include "box.h"

class Box_smhd {
  public:
    Box_smhd( );
    ~Box_smhd();
    Box * GetBox();
  private:
    Box * Container;
    void SetReserved( );
};//Box_ftyp Class

