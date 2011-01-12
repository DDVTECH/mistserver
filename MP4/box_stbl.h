#include "box.h"
#include <vector>
#include <string>

class Box_stbl {
  public:
    Box_stbl();
    ~Box_stbl();
    Box * GetBox();
    void AddContent( Box * newcontent, uint32_t offset = 0 );
  private:
    Box * Container;

    void WriteContent( );
    std::vector<Box *> Content;
};//Box_ftyp Class

