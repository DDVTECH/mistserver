enum Commands{
  CM_ERR,///<Empty Constructor for map
  CM_OCC,///<Overall Connection Connect
  CM_OCD,///<Overall Connection Disconnect
  CM_OSG,///<Selector for OSG functionality
  CM_OSGU,///<Overall Statistics Get : Users
  CM_OSGT///<Overall Statistisc Get : Throughput
};

#include <string>
#include <vector>
#include <map>
#include <cstdlib>

class Gearbox_Server {
  public:
    Gearbox_Server( );
    ~Gearbox_Server( );
    std::string ParseCommand( std::string Input );
  private:
    void InitializeMap( );
    bool Connect( std::string Username, std::string Password );
    bool Disconnect( );
    std::vector<std::string> ParseArguments( std::string Params );

    bool ParamIsString( std::string Input );
    bool ParamIsInt( std::string Input );

    std::map<std::string,Commands> CommandMap;
    bool LogIn;
    int UserID;
};//Gearbox Server Class
