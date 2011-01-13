#include "box.h"

class Box_mdhd {
  public:
    Box_mdhd( );
    ~Box_mdhd();
    Box * GetBox();
//    void SetCreationTime( uint32_t TimeStamp = 0 );
//    void SetModificationTime( uint32_t TimeStamp = 0 );
//    void SetTimeScale( uint32_t TimeUnits = 0 );
//    void SetDurationTime( uint32_t TimeUnits = 0 );
    void SetLanguage( uint8_t Firstchar = 'n', uint8_t Secondchar = 'l', uint8_t Thirdchar = 'd' );
  private:
    void SetReserved();
    void SetDefaults();
    Box * Container;
};//Box_ftyp Class

