#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <stdlib.h>
#include <string>

class GB_Client {
  public:
    GB_Client(  )
    ~GB_Client( )
    void Parse_Config( );
  private:
    char * ReadConfig( FILE * File, int & BufSize );
    std::string MyName;
    char * ConfigFile;
    int ConfigFileSize;
};//GB_Client

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
