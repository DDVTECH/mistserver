#include "box.h"

class Box_nmhd {
  public:
    Box_nmhd( );
    ~Box_nmhd();
    Box * GetBox();
  private:
    Box * Container;
    void SetReserved( );
};//Box_ftyp Class

