#include "gb_client.h"

GB_Client::GB_Client( ) {
  Parse_Config( );
}

GB_Client::~GB_Client( ) {
}

void GB_Client::PrintConfig( ) {
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
void GB_Client::Parse_Config( ) {
  FILE * TempFile = fopen( "./config.sh", "r");
  if( TempFile ) {
    ConfigFile = ReadConfig( TempFile );
    fclose( TempFile );
    Parse( );
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


void GB_Client::Run( ) {
  Calculate_Running( );
  Calculate_Stop( );
  printf( "Stopping Streams:\n" );
  for( int i = 0; i < To_Stop.size(); i++ ) {
    printf( "\t%s\n", To_Stop[i].c_str() );
  }
  Calculate_Start( );
  printf( "Starting Streams:\n" );
  for( int i = 0; i < To_Start.size(); i++ ) {
    printf( "\t%s\n", To_Start[i].c_str() );
  }
  Stop_Streams( );
}


void GB_Client::Calculate_Start( ) {
  To_Start = StreamNames;
  for( std::vector<std::string>::iterator i = To_Start.end()-1; i >= To_Start.begin(); --i ) {
    for( std::vector< std::pair<std::string,std::string> >::iterator it = Running_Streams.begin(); it != Running_Streams.end(); ++it ) {
      if( (*i) == (*it).second ) { To_Start.erase( i ); break; }
    }
  }
}

void GB_Client::Calculate_Stop( ) {
  std::vector<std::string>::iterator it;
  for( unsigned int i = 0; i < Running_Streams.size(); i++ ) {
    To_Stop.push_back( Running_Streams[i].second );
  }
  for( std::vector<std::string>::iterator i = To_Stop.end()-1; i >= To_Stop.begin(); --i ) {
    it = std::find( StreamNames.begin(), StreamNames.end(), (*i) );
    if( it != StreamNames.end() ) { To_Stop.erase( i ); }
  }
}

void GB_Client::Stop_Streams( ) {
  for( unsigned int i = 0; i < To_Stop.size(); i++ ) {
    Stop_Single_Stream( To_Stop[i] );
  }
}

void GB_Client::Stop_Single_Stream( std::string Subject ) {
  for( unsigned int i = 0; i < Running_Streams.size( ); i++ ) {
    if( Running_Streams[i].second == Subject ) {
      printf( "Running Command: kill -9 %s\n", Running_Streams[i].first.c_str() );
      system( ("kill -9 " + Running_Streams[i].first).c_str() );
    }
  }
}

void GB_Client::Calculate_Running( ) {
  system( "pidof DDV_Buffer > ./.tmpfile" );
  system( "for i in `cat ./.tmpfile`; do echo -n \"${i}:\"; ps -p $i h -o args; done > ./.tmpfile2" );
  std::string Result = FileToString( "./.tmpfile2" );
  std::string TempStr;
  std::pair<std::string,std::string> TempPair;
  int i = 0;
  while( Result.find( '\n', i) != std::string::npos ) {
    TempStr = Result.substr( i, ( Result.find( '\n', i ) - i ) );
    TempPair.first = TempStr.substr( 0, TempStr.find( ':' ) );
    TempPair.second = TempStr.substr( TempStr.rfind( ' ' ) + 1 );
    if( atoi( TempPair.first.c_str() ) != 0 ) { Running_Streams.push_back( TempPair ); }
    i = Result.find( '\n', i) + 1;
  }
  TempStr = Result.substr( i );
  TempPair.first = TempStr.substr( 0, TempStr.find( ':' ) );
  TempPair.second = TempStr.substr( TempStr.rfind( ' ' ) + 1 );
  if( atoi( TempPair.first.c_str() ) != 0 ) { Running_Streams.push_back( TempPair ); }
}

std::string GB_Client::FileToString( std::string FileName ) {
  std::ifstream Input;
  std::string Result;
  Input.open( FileName.c_str() );
  while( Input.good() ) { Result += Input.get(); }
  Input.close( );
  return Result;
}
