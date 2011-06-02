enum Commands{
  CM_ERR,
  CM_OCC,
  CM_OCD
};

#include <string>
#include <vector>
#include <map>

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
