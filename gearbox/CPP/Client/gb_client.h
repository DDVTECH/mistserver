#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <stdlib.h>
#include <string>
#include <vector>

#include "stream.h"
#include "limit.h"

class GB_Client {
  public:
    GB_Client( );
    ~GB_Client( );
    void Parse_Config( );
    void Run( );
    void PrintConfig( );
  private:
    std::string ReadConfig( FILE * File );
    void Parse( );
    void Calculate_Running( );
    void Calculate_Stop( );

    std::string GetSubstring( std::string Subject, std::string Target, std::string FirstDelim = "=", std::string SecondDelim = "\n" );
    Limit ParseLimit( std::string Subject );
    Stream ParseStreamConfig( std::string StreamName );
    std::string FileToString( std::string FileName );

///Config Variables
    std::string MyName;
    std::vector<std::string> StreamNames;
    std::vector<Stream> Streams;
    Limit GlobalLimit;
    std::vector<Limit> StreamLimits;
    std::string ConfigFile;
///Run Variables
    std::vector< std::pair<int,std::string> > Running_Streams;
    std::vector<std::string> To_Stop;
    std::vector<std::string> To_Start;
};//GB_Client
