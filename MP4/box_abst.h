#include "box.h"

class Box_abst {
  public:
    Box_abst( );
    ~Box_abst();
    Box * GetBox();
    
  private:
    uint8_t curProfile;
    bool isLive;
    bool isUpdate;
    Box * Container;
};//Box_ftyp Class
