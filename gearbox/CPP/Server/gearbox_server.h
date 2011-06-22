enum Commands{
  CM_ERR,///<Empty Constructor for map
  CM_OCC,///<Overall Connection Connect
  CM_OCD,///<Overall Connection Disconnect
  CM_SCA,///<Servers Config Add
  CM_SCR,///<Servers Config Remove
  CM_SCS,///<Servers Config Set SELECTOR
  CM_SCS_N,///<Servers Config Set Name
  CM_SCS_A,///<Servers Config Set Name
  CM_SCS_S,///<Servers Config Set Name
  CM_SCS_H,///<Servers Config Set Name
  CM_SCS_R,///<Servers Config Set Name
  CM_SCG,///<Servers Config Get SELECTOR
  CM_SCG_N,///<Servers Config Get Name
  CM_SCG_A,///<Servers Config Get Name
  CM_SCG_S,///<Servers Config Get Name
  CM_SCG_H,///<Servers Config Get Name
  CM_SCG_R,///<Servers Config Get Name
  CM_SLS,///<Severs Limit Set SELECTOR
  CM_SLS_B,///<Servers Limit Set Bandwidth
  CM_SLS_U,///<Servers Limit Set Users
  CM_SLG,///<Severs Limit Get SELECTOR
  CM_SLG_B,///<Servers Limit Get Bandwith
  CM_SLG_U,///<Servers Limit Get Users
  CM_SLG_L,///<Servers Limit Get Limits
  CM_CCA,
  CM_CCR,
  CM_CCS,
  CM_CCG,
  CM_CCS_N,
  CM_CCS_S,
  CM_CCG_N,
  CM_CCG_S,
  CM_CPA,
  CM_CPR,
  CM_GCA,
  CM_GCR,
  CM_GTA,
  CM_GTR,
  CM_GCS,
  CM_GCS_N,
  CM_GCG,
  CM_GCG_N,
  CM_GSA,
  CM_GSR,
};

#include <string>
#include <sstream>
#include <deque>
#include <vector>
#include <map>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <algorithm>
#include "../../../util/ddv_socket.h"
#include "../../../util/md5.h"
#include "server.h"
#include "channel.h"
#include "group.h"

#define TESTUSER_ID "5"
#define TESTUSER_PASS "Chocokoekjes"
#define TESTUSER_STRING "DDVTECH"

class Gearbox_Server {
  public:
    Gearbox_Server( DDV::Socket Connection );
    ~Gearbox_Server( );

    void Handshake( );
    void HandleConnection( );

  private:
    void InitializeMap( );

    void WriteReturn( );
    std::string Encode( std::string input );
    std::string Decode( std::string input );

    std::string GenerateRandomString( int charamount );
    std::string GetSingleCommand( );
    std::deque<std::string> GetParameters( std::string Cmd );

    int ServerConfigAdd( );
    bool ServerConfigRemove( std::string SrvID );
    bool ServerConfigSetName( std::string SrvId, std::string SrvName );
    bool ServerConfigSetAddress( std::string SrvId, std::string SrvAddress );
    bool ServerConfigSetSSH( std::string SrvId, int SrvSSH );
    bool ServerConfigSetHTTP( std::string SrvId, int SrvHTTP );
    bool ServerConfigSetRTMP( std::string SrvId, int SrvRTMP );
    bool ServerLimitSetBW( std::string SrvId, int SrvLimitBW );
    bool ServerLimitSetUsers( std::string SrvId, int SrvLimitUsers );

    int ChannelConfigAdd( );
    bool ChannelConfigRemove( std::string ChID );
    bool ChannelConfigSetName( std::string ChID, std::string ChName );
    bool ChannelConfigSetSource( std::string ChID, std::string ChSrc );
    bool ChannelPresetAdd( std::string ChID, std::string PrsName );
    bool ChannelPresetRemove( std::string ChID, std::string PrsName );

    int GroupConfigAdd( );
    bool GroupConfigRemove( std::string GrpID );
    bool GroupConfigSetName( std::string GrpID, std::string GrpName );

    std::map<int,Server>::iterator RetrieveServer( std::string Index );
    std::map<int,Channel>::iterator RetrieveChannel( std::string Index );
    std::map<int,Group>::iterator RetrieveGroup( std::string Index );

    void WriteConfig( );

    bool IsSrv;
    std::string RetVal;
    std::string RandomConnect;
    std::string RandomAuth;
    std::string XorPath;
    DDV::Socket conn;
    std::map<std::string,Commands> CommandMap;
    std::map<int,Server> ServerConfigs;
    std::map<int,std::string> ServerNames;
    std::map<int,Channel> ChannelConfigs;
    std::map<int,std::string> ChannelNames;
    std::map<int,Group> GroupConfigs;
    std::map<int,std::string> GroupNames;

    std::vector<std::string> Presets;
};//Gearbox Server Class
