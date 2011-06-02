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
  CommandMap["OSG"] = CM_OSG;
  CommandMap["OSG_U"] = CM_OSGU;
  CommandMap["OSG_T"] = CM_OSGT;
}

bool Gearbox_Server::ParamIsString( std::string Input ) {
  if( atoi( Input.c_str() ) != 0 ) { return false; }
  return true;
}

bool Gearbox_Server::ParamIsInt( std::string Input ) {
  if( atoi( Input.c_str() ) == 0 ) { return false; }
  return true;
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
  bool exists_selector = false;
  switch( CommandMap[Input.substr(0,3).c_str()] ) {
    case CM_OCC:
      if( LogIn ) { Result = "ER_AlreadyLoggedIn"; break; }
      Params = ParseArguments( Input.substr(3) );
      if( Params.size() != 2 ) { Result = "ER_InvalidArguments"; break; }
      if( !ParamIsString( Params[0] ) || !ParamIsString( Params[1] ) ) { Result = "ER_InvalidData"; break; }
      if( !Connect( Params[0],Params[1] ) ) { Result = "ER_InvalidCredentials"; break; }
      Result = "OK";
      break;
    case CM_OCD:
      if( !LogIn ) { Result = "ER_NotLoggedIn"; break; }
      Params = ParseArguments( Input.substr(3) );
      if( Params.size() != 0 ) { Result = "ER_InvalidArguments"; break; }
      if( !Disconnect( ) ) { Result = "ER"; break; }
      Result = "OK";
      break;
    case CM_OSG:
      exists_selector = true;
      break;
    default:
      Result = "ER_InvalidCommand";
      break;
  }
  if( exists_selector ) {
    switch( CommandMap[Input.substr(0,5).c_str()] ) {
      case CM_OSG_U:
        Result = "OK";
        break;
      case CM_OSG_T:
        Result = "OK";
        break;
      default:
        Result = "ER_InvalidCommand";
        break;
    }
  }
  return Result;
}

bool Gearbox_Server::Disconnect( ) {
  LogIn = false;
  return true;
}

bool Gearbox_Server::Connect( std::string Username, std::string Password ) {
  LogIn = true;
  return true;
}
