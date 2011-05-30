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
    std::string GetSubstring( std::string Subject, std::string Target, std::string FirstDelim = "=", std::string SecondDelim = "\n" );
    Limit ParseLimit( std::string Subject );
  private:
    std::string ReadConfig( FILE * File );
    Stream ParseStreamConfig( std::string StreamName );
    void Parse( );
    std::string MyName;
    std::vector<std::string> StreamNames;
    std::vector<Stream> Streams;
    Limit GlobalLimit;
    std::vector<Limit> StreamLimits;
    std::string ConfigFile;
};//GB_Client
