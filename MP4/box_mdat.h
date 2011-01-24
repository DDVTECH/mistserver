#include "box.h"
#include <vector>
#include <string>

class Box_mdat {
  public:
    Box_mdat();
    ~Box_mdat();
    Box * GetBox();
    void SetContent( uint8_t * NewData, uint32_t DataLength , uint32_t offset = 0 );
  private:
    Box * Container;
};//Box_ftyp Class

