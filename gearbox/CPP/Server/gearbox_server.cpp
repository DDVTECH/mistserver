#include "gearbox_server.h"

Gearbox_Server::Gearbox_Server( DDV::Socket Connection ) {
  srand( time( NULL ) );
  conn = Connection;
  RandomConnect = GenerateRandomString( 8 );
  RandomAuth = GenerateRandomString( 8 );
}

Gearbox_Server::~Gearbox_Server( ) {}

void Gearbox_Server::InitializeMap( ) {
  CommandMap["OCC"] = CM_OCC;
  CommandMap["OCD"] = CM_OCD;
}


std::string Gearbox_Server::GenerateRandomString( int charamount ) {
  std::string Result;
  for( int i = 0; i < charamount; i++ ) {
    Result += (char)((rand() % 93)+33);
  }
  return Result;
}

std::string Gearbox_Server::GetSingleCommand( ) {
  static std::string CurCmd;
  std::string Result = "";
  if( conn.ready( ) ) {
    conn.read( CurCmd );
    if( CurCmd.find('\n') != std::string::npos ) {
      Result = CurCmd.substr(0, CurCmd.find('\n') );
      while( CurCmd[0] != '\n' ) { CurCmd.erase( CurCmd.begin( ) ); }
      CurCmd.erase( CurCmd.begin( ) );
    }
  }
  return Result;
}

void Gearbox_Server::WriteReturn( ) {
  while( !conn.ready( ) ) {}
  conn.write( RetVal + "\n" );
}

void Gearbox_Server::Handshake( ) {
  std::deque<std::string> ConnectionParams;
  RetVal = "WELCOME" + RandomConnect;
  WriteReturn( );
  std::string Cmd;
  while( Cmd == "" ) { Cmd = GetSingleCommand( ); }
  if( Cmd.substr(0,3) != "OCC" ) {
    RetVal = "ERR";
    WriteReturn( );
    exit( 1 );
  }
  ConnectionParams = GetParameters( Cmd.substr(4) );
  if( ConnectionParams.size( ) != 2 ) {
    RetVal = "ERR_ParamAmount";
    WriteReturn( );
    exit( 1 );
  }
  if( ConnectionParams[0] != TESTUSER_ID ) {
    RetVal = "ERR_Credentials";
    WriteReturn( );
    exit( 1 );
  }
  if( ConnectionParams[1] == md5( RandomConnect + TESTUSER_STRING + RandomConnect ) ) {
    IsSrv = true;
    RetVal = "OCC" + RandomAuth;
    WriteReturn( );
    XorPath = md5( RandomAuth + TESTUSER_STRING + RandomAuth );
  } else if ( ConnectionParams[1] ==  md5( RandomConnect + TESTUSER_PASS + RandomConnect ) ) {
    IsSrv = false;
    RetVal = "OCC" + RandomAuth;
    WriteReturn( );
    XorPath = md5( RandomAuth + TESTUSER_PASS + RandomAuth );
  } else {
    RetVal = "ERR_Credentials";
    WriteReturn( );
    exit( 1 );
  }
}

std::deque<std::string> GetParameters( std::string Cmd ) {
  for( std::string::iterator it = Cmd.end( ) - 1; it >= Cmd.begin( ); it -- ) { if( (*it) == '\r' ) { Cmd.erase( it ); } }
  std::string temp;
  std::deque<std::string> Result;
  for( std::string::iterator it = Cmd.begin( ); it != Cmd.end( ); it ++ ) {
    if( (*it) == ':' ) {
      if( temp != "" ) { Result.push_back( temp ); temp = ""; }
    } else {
      temp += (*it);
    }
  }
  if( temp != "" ) { Result.push_back( temp ); }
  return Result;
}
