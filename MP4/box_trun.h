#include "box.h"
#include <vector>

struct trun_sampleinformationstructure {
  uint32_t SampleDuration;
  uint32_t SampleSize;
};

class Box_trun {
  public:
    Box_trun( );
    ~Box_trun();
    Box * GetBox();
    void SetDataOffset( uint32_t Offset = 0 );
    void AddSampleInformation( uint32_t SampleDuration = 0, uint32_t SampleSize = 0, uint32_t Offset = 0 );
    void WriteContent( );
  private:
    void SetDefaults( );
    bool setSampleDuration;
    bool setSampleSize;
    uint32_t curDataOffset;
    std::vector<trun_sampleinformationstructure> SampleInfo;
    Box * Container;
};//Box_ftyp Class

