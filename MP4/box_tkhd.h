#include "box.h"
#include <ctime>

#define SECONDS_DIFFERENCE 2082844800

class Box_tkhd {
  public:
    Box_tkhd( );
    ~Box_tkhd();
    Box * GetBox();
    void SetCreationTime( uint32_t TimeStamp = 0 );
    void SetModificationTime( uint32_t TimeStamp = 0 );
    void SetDurationTime( uint32_t TimeUnits = 0 );
    void SetWidth( uint32_t Width = 0 );
    void SetHeight( uint32_t Height = 0 );
    void SetFlags( bool Bit0 = true, bool Bit1 = true, bool Bit2 = true );
    void SetVersion( uint32_t Version = 0 );
    void SetTrackID( uint32_t TrackID = 0 );
  private:
    void SetReserved();
    void SetDefaults();
    Box * Container;

    uint32_t CurrentFlags;
    uint32_t CurrentVersion;
};//Box_ftyp Class

