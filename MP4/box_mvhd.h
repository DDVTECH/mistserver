#include "box.h"
#include <ctime>

#define SECONDS_DIFFERENCE 2082844800

class Box_mvhd {
  public:
    Box_mvhd( );
    ~Box_mvhd();
    Box * GetBox();
    void SetCreationTime( uint32_t TimeStamp = 0 );
    void SetModificationTime( uint32_t TimeStamp = 0 );
    void SetTimeScale( uint32_t TimeUnits = 1 );
    void SetDurationTime( uint32_t TimeUnits = 0 );
    void SetRate( uint32_t Rate = 0x00010000 );
    void SetVolume( uint16_t Volume = 0x0100 );
    void SetNextTrackID( uint32_t TrackID = 0 );
  private:
    void SetReserved();
    void SetDefaults();
    Box * Container;

};//Box_ftyp Class

