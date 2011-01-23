#include "box.h"
#include <vector>
#include <string>

class Box_minf {
  public:
    Box_minf();
    ~Box_minf();
    Box * GetBox();
    void AddContent( Box * newcontent, uint32_t offset = 0 );
    void WriteContent( );
  private:
    Box * Container;

    std::vector<Box *> Content;
};//Box_ftyp Class

