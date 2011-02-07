#include "box.h"
#include <vector>
#include <string>

class Box_moof {
  public:
    Box_moof();
    ~Box_moof();
    Box * GetBox();
    void AddContent( Box * newcontent, uint32_t offset = 0 );
    void WriteContent( );
  private:
    Box * Container;

    std::vector<Box *> Content;
};//Box_ftyp Class

