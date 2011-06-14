#include "gearbox_server.h"

Gearbox_Server::Gearbox_Server( DDV::Socket Connection ) {
  srand( time( NULL ) );
  conn = Connection;
  RandomConnect = GenerateRandomString( 8 );
  RandomAuth = GenerateRandomString( 8 );
  XorPath = "";
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
  std::string DecCmd;
  std::string Result = "";
  if( conn.ready( ) ) {
std::cout << "Connection Ready\n";
    conn.read( CurCmd );
    if( XorPath != "" ) {
      DecCmd = CurCmd;
      CurCmd = Decode( DecCmd );
    }
    if( CurCmd.find('\n') != std::string::npos ) {
      Result = CurCmd.substr(0, CurCmd.find('\n') );
      while( CurCmd[0] != '\n' ) {
        CurCmd.erase( CurCmd.begin( ) );
        if( DecCmd != "" ) {
          DecCmd.erase( DecCmd.begin( ) );
        }
      }
      CurCmd.erase( CurCmd.begin( ) );
      if( DecCmd != "" ) {
        DecCmd.erase( DecCmd.begin( ) );
      }
    }
    if( XorPath != "" ) {
      CurCmd = DecCmd;
    }
  }
  return Result;
}

std::string Gearbox_Server::Encode( std::string input ) {
  static int counter = 0;
  std::string Result;
  for( unsigned int i = 0; i < input.size( ); i ++) {
    Result.push_back( (char)( input[i] ^ XorPath[counter] ) );
    counter = (counter + 1) % XorPath.size( );
  }
  return Result;
}

std::string Gearbox_Server::Decode( std::string input ) {
std::cout << "decoding\n";
  static int counter = 0;
  std::string Result;
  for( unsigned int i = 0; i < input.size( ); i ++) {
    Result.push_back( (char)( input[i] ^ XorPath[counter] ) );
    counter = (counter + 1) % XorPath.size( );
  }
  return Result;
}

void Gearbox_Server::WriteReturn( ) {
  if( XorPath == "" ) {
    conn.write( RetVal + "\n" );
  } else {
    conn.write(  Encode( RetVal + "\n" ) );
  }
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
  ConnectionParams = GetParameters( Cmd.substr(3) );
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
  std::cout << ( IsSrv ? "Server Connected\n" : "Customer Connected\n" );
  std::cout << "\tCalculated xorpath: " << XorPath << "\n";
}

std::deque<std::string> Gearbox_Server::GetParameters( std::string Cmd ) {
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

void Gearbox_Server::HandleConnection( ) {
  std::string Cmd = GetSingleCommand( );
  if( Cmd == "" ) { return; }
  switch( CommandMap[ Cmd.substr(0,3) ] ) {
    default:
      RetVal = "ERR_InvalidCommand:" + Cmd;
  }
  WriteReturn( );
}
