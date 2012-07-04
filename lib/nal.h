#include <string>

class NAL_Unit {
  public:
    NAL_Unit( );
    NAL_Unit( std::string & InputData );
    bool ReadData( std::string & InputData );
    std::string AnnexB( bool LongIntro = false );
    std::string SizePrepended( );
    int Type( );
  private:
    std::string MyData;
};//NAL_Unit class
