#include "box.h"
#include <string>

class Box_hdlr {
  public:
    Box_hdlr( );
    ~Box_hdlr();
    Box * GetBox();
    void SetHandlerType( uint32_t HandlerType );
    void SetName ( std::string Name );
  private:
    Box * Container;
    void SetReserved( );
    uint32_t CurrentHandlerType;
};//Box_ftyp Class

