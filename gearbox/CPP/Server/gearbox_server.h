enum Commands{
  CM_ERR,///<Empty Constructor for map
  CM_OCC,///<Overall Connection Connect
  CM_OCD,///<Overall Connection Disconnect
};


#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include "../../../util/ddv_socket.h"

#define TESTUSER_ID "5"
#define TESTUSER_PASS "Chocokoekjes"
#define TESTUSER_STRING "DDVTECH"

class Gearbox_Server {
  public:
    Gearbox_Server( DDV::Socket Connection );
    ~Gearbox_Server( );

    void Handshake( );

  private:
    void InitializeMap( );

    void WriteReturn( );

    std::string GenerateRandomString( int charamount );
    std::string GetSingleCommand( );

    bool IsSrv;
    std::string RandomConnect;
    std::string RandomAuth;
    std::string XorPath;
    DDV::Socket conn;
    std::map<std::string,Commands> CommandMap;
};//Gearbox Server Class
