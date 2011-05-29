#include "gb_client.h"

GB_Client::GB_Client( ) {
  Parse_Config( );
}

void GB_Client::Parse_Config( ) {
  FILE * TempFile = fopen( "./config.sh" );
  ConfigFile = ReadConfig( TempFile, ConfigFileSize );
  fclose( TempFile );
}

char * GB_Client::ReadConfig( FILE * File, int & BufSize ) {
  if( !File ) { BufSize = 0; return NULL; }
  fseek (File , 0 , SEEK_END);
  BufSize = ftell (File);
  rewind (File);

  char * Result = (char*) malloc (sizeof(char)*BufSize);
  fread (Result,1,BufSize,File);

  return Result;
}
