#include "box.h"

class Box_trex {
  public:
    Box_trex( );
    ~Box_trex();
    Box * GetBox();
    void SetTrackID( uint32_t Id = 0 );
    void SetSampleDescriptionIndex( uint32_t Index = 0 );
    void SetSampleDuration( uint32_t Duration = 0 );
    void SetSampleSize( uint32_t Size = 0 );
  private:
    void SetReserved( );
    void SetDefaults( );
    Box * Container;
};//Box_ftyp Class
