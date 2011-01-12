#include "box.h"

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
};//Box_ftyp Class

