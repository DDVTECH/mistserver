#include "box.h"
#include <vector>
#include <string>

class Box_stsd {
  public:
    Box_stsd( );
    ~Box_stsd();
    Box * GetBox();
    void AddContent( Box * newcontent, uint32_t offset = 0 );
  private:
    Box * Container;

    void SetReserved();
    void WriteContent();
    std::vector<Box *> Content;
};//Box_ftyp Class

