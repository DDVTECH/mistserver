#include "box.h"

class Box_abst {
  public:
    Box_abst( );
    ~Box_abst();
    Box * GetBox();
  private:
    Box * Container;
};//Box_ftyp Class
