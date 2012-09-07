#include <map>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include "./socket.h"
#include "./filesystem.h"
#include <unistd.h>

#include "./json.h"

namespace FTP {
  static std::string FTPBasePath = "/tmp/mist/OnDemand/";
  
  enum Mode {
    MODE_STREAM,
  };//FTP::Mode enumeration
  
  enum Structure {
    STRU_FILE,
    STRU_RECORD,
  };//FTP::Structure enumeration
  
  enum Type {
    TYPE_ASCII_NONPRINT,
    TYPE_IMAGE_NONPRINT,
  };//FTP::Type enumeration
  
  enum Commands {
    CMD_NOCMD,
    CMD_NOOP,
    CMD_USER,
    CMD_PASS,
    CMD_QUIT,
    CMD_PORT,
    CMD_RETR,
    CMD_STOR,
    CMD_TYPE,
    CMD_MODE,
    CMD_STRU,
    CMD_EPSV,
    CMD_PASV,
    CMD_LIST,
    CMD_PWD,
    CMD_CWD,
    CMD_CDUP,
    CMD_DELE,
    CMD_RMD,
    CMD_MKD,
    CMD_RNFR,
    CMD_RNTO,
  };//FTP::Commands enumeration
  
  class User {
    public:
      User( Socket::Connection NewConnection, std::map<std::string,std::string> Credentials);
      ~User( );
      int ParseCommand( std::string Command );
      bool LoggedIn( );
      std::string NumToMsg( int MsgNum );
      Socket::Connection Conn;
    private:
      std::map<std::string,std::string> AllCredentials;
      std::string USER;
      std::string PASS;
      Mode MODE;
      Structure STRU;
      Type TYPE;
      int PORT;
      Socket::Server Passive;
      int MyPassivePort;
      Filesystem::Directory MyDir;
      std::string RNFR;
      std::vector< std::string > ActiveStreams;
  };//FTP::User class
  
};//FTP Namespace
