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
  TempPos1 = ConfigFile.find( '=' , ConfigFile.find( "myname" ) ) + 1;
  TempPos2 = ConfigFile.find( '\n', TempPos1 );
  Length = TempPos2 - TempPos1;
  MyName = ConfigFile.substr(TempPos1,Length);
}
