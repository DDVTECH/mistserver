#include "box.h"
#include <string>
#include <vector>

struct amhp_record {
  uint8_t HintTrackMode;
  uint8_t Settings;
  uint8_t TrailerDefaultSize;
};//stsc_record

class Box_amhp {
  public:
    Box_amhp( );
    ~Box_amhp();
    Box * GetBox();
    void SetReserved( );
    void AddEntry( uint8_t HintTrackMode, uint8_t Settings, uint8_t TrailerDefaultSize, uint32_t Offset = 0 );
    void WriteContent( );
  private:
    Box * Container;

    std::vector<amhp_record> Entries;
};//Box_ftyp Class

