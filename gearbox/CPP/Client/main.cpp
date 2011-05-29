///\file main.cpp
/// Holds all the code for the client-side of gearbox

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <stdlib.h>

///A function for reading a file into a dynamically allocated buffer
///Returns a character buffer containing the contents of the file
///Or NULL if File is NULL
///\param File the file to read from
///\param BufSize An integer to store the buffersize in 
char* ReadConfig( FILE * File, int & BufSize ) {
  if( !File ) { BufSize = 0; return NULL; }
  fseek (File , 0 , SEEK_END);
  BufSize = ftell (File);
  rewind (File);

  char * Result = (char*) malloc (sizeof(char)*BufSize);
  fread (Result,1,BufSize,File);

  return Result;
};


int main( ) {
  int BufferSize;
  FILE * ConfigFile = fopen( "./config.sh", "r" );
  if( ConfigFile ) {
    printf( "Config File Opened\n" );
    char * test = ReadConfig( ConfigFile, BufferSize );
    fclose( ConfigFile );
    free(test);
  } else {
    printf( "Config File Not Found\n" );
  }
  return 0;
}
