#include "box.h"
#include <string>
#include <vector>

struct asrt_segmentrunentry {
  uint32_t FirstSegment;
  uint32_t FragmentsPerSegment;
};//abst_qualityentry

class Box_asrt {
  public:
    Box_asrt( );
    ~Box_asrt();
    Box * GetBox();
    void SetUpdate( bool Update = false );
    void AddQualityEntry( std::string Quality = "", uint32_t Offset = 0 );
    void AddSegmentRunEntry( uint32_t FirstSegment = 0, uint32_t FragmentsPerSegment = 100, uint32_t Offset = 0 );
    void WriteContent( );
  private:
    void SetDefaults( );
    bool isUpdate;
    std::vector<std::string> QualitySegmentUrlModifiers;
    std::vector<asrt_segmentrunentry> SegmentRunEntryTable;
    Box * Container;
};//Box_ftyp Class
