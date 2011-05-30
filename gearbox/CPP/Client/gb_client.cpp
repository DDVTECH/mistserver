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
    printf( "Limits:\n" );
    printf( "\tMax_Users: %d\n", GlobalLimit.Max_Users );
    printf( "\tMax_Bw: %d\n", GlobalLimit.Max_Bw );
    for( unsigned int i = 0; i < StreamNames.size( ); i++ ) {
      printf( "Stream %d: %s\n", i, StreamNames[i].c_str() );
      printf( "\tName: %s\n", Streams[i].Name.c_str() );
      printf( "\tInput: %s\n", Streams[i].Input.c_str() );
      printf( "\tPreset: %s\n", Streams[i].Preset.c_str() );
      printf( "\tMax_Users: %d\n", StreamLimits[i].Max_Users );
      printf( "\tMax_Bw: %d\n", StreamLimits[i].Max_Bw );
    }
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

std::string GB_Client::GetSubstring( std::string Subject, std::string Target, std::string FirstDelim, std::string SecondDelim ) {
  int Pos1,Pos2;
  Pos1 = Subject.find( FirstDelim, Subject.find( Target ) ) + FirstDelim.length( );
  Pos2 = Subject.find_first_of( SecondDelim, Pos1 );
  return Subject.substr(Pos1, (Pos2-Pos1) );
}

void GB_Client::Parse( ) {
  std::string TempStr;

  MyName = GetSubstring( ConfigFile, "myname" );
  TempStr = GetSubstring( ConfigFile, "server_" + MyName, "\"", "\"" );
  unsigned int i=0;
  while( TempStr.find( ' ', i) != std::string::npos ) {
    StreamNames.push_back( TempStr.substr( i, TempStr.find( ' ', i ) - i ) );
    i = TempStr.find( ' ' ) + 1;
  }
  StreamNames.push_back( TempStr.substr( i ) );
  for( i = 0; i < StreamNames.size( ); i++ ) {
    Streams.push_back( ParseStreamConfig( StreamNames[i] ) );
  }
  TempStr = GetSubstring( ConfigFile, "limits_" + MyName + "=", "\"", "\"" ) + "\n";
  GlobalLimit = ParseLimit( TempStr );
  for( i = 0; i < StreamNames.size( ); i++ ) {
    TempStr = GetSubstring( ConfigFile, "limits_" + MyName + "_" + StreamNames[i] + "=", "\"", "\"" ) + "\n";
    StreamLimits.push_back( ParseLimit( TempStr ) );
  }
}


Limit GB_Client::ParseLimit( std::string Subject ) {
  Limit Result;
  Result.Max_Users = atoi( GetSubstring( Subject, "MAX_USERS", "=", " \n" ).c_str() );
  Result.Max_Bw = atoi( GetSubstring( Subject, "MAX_BW", "=", " \n" ).c_str() );
  return Result;
}

Stream GB_Client::ParseStreamConfig( std::string StreamName ) {
  Stream Result;
  std::string TempStr = GetSubstring( ConfigFile, "config_" + MyName + "_" + StreamName, "(", ")" ) + "\n";
  Result.Name = GetSubstring( TempStr, "NAME", "=", " \n" );
  Result.Input = GetSubstring( TempStr, "INPUT", "=", " \n" );
  Result.Preset = GetSubstring( TempStr, "PRESET", "=", " \n" );
  return Result;
}
