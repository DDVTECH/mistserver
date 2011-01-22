#include "box.h"
#include <string>
#include <vector>

struct stsc_record {
  uint32_t FirstChunk;
  uint32_t SamplesPerChunk;
  uint32_t SampleDescIndex;
};//stsc_record

class Box_stsc {
  public:
    Box_stsc( );
    ~Box_stsc();
    Box * GetBox();
    void SetReserved( );
    void AddEntry( uint32_t FirstChunk = 0, uint32_t SamplesPerChunk = 0, uint32_t SampleDescIndex = 0, uint32_t Offset = 0 );
  private:
    Box * Container;

    void WriteEntries( );
    std::vector<stsc_record> Entries;
};//Box_ftyp Class

