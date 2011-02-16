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
    void SetBootstrapVersion( uint32_t Version = 1 );
    void SetProfile( uint8_t Profile = 0 );
    void SetLive( bool Live = true );
    void SetUpdate( bool Update = false );
    void SetTimeScale( uint32_t Scale = 1000 );
    void SetMediaTime( uint32_t Time = 0 );
    void SetSMPTE( uint32_t Smpte = 0 );
    void SetMovieIdentifier( std::string Identifier = "" );
    void SetDRM( std::string Drm = "" );
    void SetMetaData( std::string MetaData = "" );
    void AddServerEntry( std::string Url = "", uint32_t Offset = 0 );
    void AddQualityEntry( std::string Quality = "", uint32_t Offset = 0 );
    void AddSegmentRunTable( Box * newSegment, uint32_t Offset = 0 );
    void AddFragmentRunTable( Box * newFragment, uint32_t Offset = 0 );
    void WriteContent( );
  private:
    void SetDefaults( );
    void SetReserved( );
    uint32_t curBootstrapInfoVersion;
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
