#include "box.h"
#include <vector>
#include <string>

class Box_dinf {
  public:
    Box_dinf();
    ~Box_dinf();
    Box * GetBox();
    void AddContent( Box * newcontent, uint32_t offset = 0 );
  private:
    Box * Container;

    void WriteContent( );
    Box * Content;
};//Box_ftyp Class

