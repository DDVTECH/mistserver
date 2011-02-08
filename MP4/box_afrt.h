#include "box.h"
#include <string>
#include <vector>

struct afrt_fragmentrunentry {
  uint32_t FirstFragment;
  uint32_t FirstFragmentTimestamp; //write as uint64_t
  uint32_t FragmentDuration;
  uint8_t DiscontinuityIndicator;//if FragmentDuration == 0
};//afrt_fragmentrunentry

class Box_afrt {
  public:
    Box_afrt( );
    ~Box_afrt();
    Box * GetBox();
    void SetUpdate( bool Update = false );
    void SetTimeScale( uint32_t Scale = 1000 );
    void AddQualityEntry( std::string Quality = "", uint32_t Offset = 0 );
    void AddFragmentRunEntry( uint32_t FirstFragment = 0, uint32_t FirstFragmentTimestamp = 0, uint32_t FragmentsDuration = 1, uint8_t Discontinuity = 0, uint32_t Offset = 0 );
    void WriteContent( );
  private:
    void SetDefaults( );
    bool isUpdate;
    uint32_t curTimeScale;
    std::vector<std::string> QualitySegmentUrlModifiers;
    std::vector<afrt_fragmentrunentry> FragmentRunEntryTable;
    Box * Container;
};//Box_ftyp Class
