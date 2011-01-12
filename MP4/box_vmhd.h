#include "box.h"

class Box_vmhd {
  public:
    Box_vmhd( );
    ~Box_vmhd();
    Box * GetBox();
    void SetGraphicsMode( uint16_t GraphicsMode = 0 );
    void SetOpColor( uint16_t Red = 0, uint16_t Green = 0, uint16_t Blue = 0);
  private:
    Box * Container;
    void SetReserved( );
    void SetDefaults( );
};//Box_ftyp Class

