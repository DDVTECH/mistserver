#include <cstdio>
#include <cstring>

#include "converter.h"
#include "procs.h"

namespace Converter {
  
  Converter::Converter(){
    fillFFMpegEncoders();
  }
  
  void Converter::fillFFMpegEncoders(){
    std::vector<std::string> cmd;
    cmd.push_back("ffmpeg");
    cmd.push_back("-encoders");
    int outFD = -1;
    Util::Procs::StartPiped("FFMpegInfo", cmd, 0, &outFD, 0);
    FILE * outFile = fdopen( outFD, "r" );
    char * fileBuf = 0;
    size_t fileBufLen = 0;
    while ( !(feof(outFile) || ferror(outFile))){
      getline(&fileBuf, &fileBufLen, outFile);
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
  
  
  converterInfo & Converter::getCodecs() {
    return allCodecs;
  }
}
