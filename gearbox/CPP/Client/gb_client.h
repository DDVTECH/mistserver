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

