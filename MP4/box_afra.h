#include "box.h"
#include <vector>

struct afra_record {
  uint32_t Time;
  uint32_t Offset;
};//afra_record

class Box_afra {
  public:
    Box_afra( );
    ~Box_afra();
    Box * GetBox();
    void SetTimeScale( uint32_t Scale = 1 );
    void AddEntry( uint32_t Time = 0, uint32_t SampleOffset = 0, uint32_t Offset = 0 );
    void WriteContent( );
  private:
    void SetReserved( );
    void SetDefaults( );

    Box * Container;
    uint32_t CurrentTimeScale;
    std::vector<afra_record> Entries;
};//Box_ftyp Class
