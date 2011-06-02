#include "gearbox_server.h"

Gearbox_Server::Gearbox_Server( ) {
  InitializeMap( );
  UserID = -1;
  LogIn = false;
}

Gearbox_Server::~Gearbox_Server( ) {}

void Gearbox_Server::InitializeMap( ) {
  CommandMap["OCC"] = CM_OCC;
  CommandMap["OCD"] = CM_OCD;
}

std::vector<std::string> Gearbox_Server::ParseArguments( std::string Params ) {
  for( std::string::iterator it = Params.end()-1; it >= Params.begin(); it--) {
    if((*it)=='\r') { Params.erase(it); }
  }
  std::vector<std::string> Result;
  int i = 0;
  int end;
  while( Params.find( ':' , i ) != std::string::npos ) {
    end = Params.find( ':', i );
    if( Params.substr(i, end - i) != "" ) { Result.push_back( Params.substr( i, end - i ) ); }
    i = end + 1;
  }
  if( Params.substr(i,end - i) != "" ) { Result.push_back( Params.substr( i ) ); }
  return Result;
}

std::string Gearbox_Server::ParseCommand( std::string Input ) {
  std::string Result;
  std::vector<std::string> Params;
  switch( CommandMap[Input.substr(0,3).c_str()] ) {
    case CM_OCC:
      if( LogIn ) { Result = "ER_AlreadyLoggedIn"; break; }
      Params = ParseArguments( Input.substr(3) );
      if( Params.size() != 2 ) { Result = "ER_InvalidArguments"; break; }
      LogIn = true;
      Result = "OK";
      break;
    case CM_OCD:
      if( !LogIn ) { Result = "ER_NotLoggedIn"; break; }
      Params = ParseArguments( Input.substr(3) );
      if( Params.size() != 0 ) { Result = "ER_InvalidArguments"; break; }
      Disconnect( );
      Result = "OK";
      break;
    default:
      Result = "ER_InvalidCommand";
      break;
  }
  return Result;
}

void Gearbox_Server::Disconnect( ) {
  LogIn = false;
}
