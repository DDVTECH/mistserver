#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "timing.h"
#include "converter.h"
#include "procs.h"

namespace Converter {
  
  Converter::Converter(){
    fillFFMpegEncoders();
  }
  
  void Converter::fillFFMpegEncoders(){    
    char ** cmd = (char**)malloc(3*sizeof(char*));
    cmd[0] = "ffmpeg";
    cmd[1] = "-encoders";
    cmd[2] = NULL;
    int outFD = -1;
    Util::Procs::StartPiped("FFMpegInfo", cmd, 0, &outFD, 0);
    while( Util::Procs::isActive("FFMpegInfo")){ Util::sleep(100); }
    FILE * outFile = fdopen( outFD, "r" );
    char * fileBuf = 0;
    size_t fileBufLen = 0;
    while ( !(feof(outFile) || ferror(outFile)) && (getline(&fileBuf, &fileBufLen, outFile) != -1)){
      if (strstr(fileBuf, "aac") || strstr(fileBuf, "AAC")){
        strtok(fileBuf, " \t");
        allCodecs["ffmpeg"][strtok(NULL, " \t")] = "aac";
      }
      if (strstr(fileBuf, "h264") || strstr(fileBuf, "H264")){
        strtok(fileBuf, " \t");
        allCodecs["ffmpeg"][strtok(NULL, " \t")] = "h264";
      }
      if (strstr(fileBuf, "mp3") || strstr(fileBuf, "MP3")){
        strtok(fileBuf, " \t");
        allCodecs["ffmpeg"][strtok(NULL, " \t")] = "mp3";
      }
    }
    fclose( outFile );
  }
  
  converterInfo & Converter::getCodecs(){
    return allCodecs;
  }

  JSON::Value Converter::getEncoders(){
    JSON::Value Result;
    for (converterInfo::iterator convIt = allCodecs.begin(); convIt != allCodecs.end(); convIt++){
      for (codecInfo::iterator codIt = convIt->second.begin(); codIt != convIt->second.end(); codIt++){
        Result[convIt->first][codIt->first] = codIt->second;
      }
    }
    return Result;
  }
}
