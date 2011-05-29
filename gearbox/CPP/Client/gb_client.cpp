#include "gb_client.h"

GB_Client::GB_Client( ) {
  Parse_Config( );
}

GB_Client::~GB_Client( ) {
}

void GB_Client::Parse_Config( ) {
  FILE * TempFile = fopen( "./config.sh", "r");
  if( TempFile ) {
    ConfigFile = ReadConfig( TempFile );
    fclose( TempFile );
    Parse( );
    printf( "MyName = %s\n", MyName.c_str( ) );
  }
}

std::string GB_Client::ReadConfig( FILE * File ) {
  int BufSize;
  if( !File ) { BufSize = 0; return ""; }
  fseek (File , 0 , SEEK_END);
  BufSize = ftell (File);
  rewind (File);

  char * Buffer = (char*) malloc (sizeof(char)*BufSize);
  fread (Buffer,1,BufSize,File);

  std::string Result = Buffer;
  return Result;
}

void GB_Client::Parse( ) {
  int TempPos1;
  int TempPos2;
  int Length;
  std::string TempStr;
  TempPos1 = ConfigFile.find( '=' , ConfigFile.find( "myname" ) ) + 1;
  TempPos2 = ConfigFile.find( '\n', TempPos1 );
  Length = TempPos2 - TempPos1;
  MyName = ConfigFile.substr(TempPos1,Length);
  TempPos1 = ConfigFile.find( '\"' , ConfigFile.find( "server_" + MyName ) ) + 1;
  TempPos2 = ConfigFile.find( "\"\n", TempPos1 );
  Length = TempPos2 - TempPos1;
  TempStr = ConfigFile.substr(TempPos1,Length);
  printf( "Server_MyName = %s\n", TempStr.c_str() );
  int i=0;
  while( TempStr.find( ' ', i) != std::string::npos ) {
    StreamNames.push_back( TempStr.substr( i, TempStr.find( ' ', i ) - i ) );
    i = TempStr.find( ' ' ) + 1;
  }
  StreamNames.push_back( TempStr.substr( i ) );
  for( int i = 0; i < StreamNames.size( ); i++ ) {
    printf( "Stream %d: %s\n", i+1, StreamNames[i].c_str( ) );
  }
}
