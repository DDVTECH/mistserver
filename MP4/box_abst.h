#include "box.h"
#include <string>
#include <vector>

struct abst_serverentry {
  std::string ServerBaseUrl;
};//abst_serverentry

struct abst_qualityentry {
  std::string QualityModifier;
};//abst_qualityentry

class Box_abst {
  public:
    Box_abst( );
    ~Box_abst();
    Box * GetBox();

  private:
    uint8_t curProfile;
    bool isLive;
    bool isUpdate;
    uint32_t curTimeScale;
    uint32_t curMediatime;//write as uint64_t
    uint32_t curSMPTE;//write as uint64_t
    std::string curMovieIdentifier;
    std::string curDRM;
    std::string curMetaData;
    std::vector<abst_serverentry> Servers;
    std::vector<abst_qualityentry> Qualities;
    std::vector<Box *> SegmentRunTables;
    std::vector<Box *> FragmentRunTables;
    Box * Container;
};//Box_ftyp Class
