#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

#include <mist/json.h>
#include <mist/procs.h>

namespace Info {
  int getInfo(int argc, char* argv[]) {
    if (argc < 2){
      fprintf( stderr, "Usage: %s <filename>\n", argv[0] );
      return 1;
    }
    JSON::Value fileSpecs;
    std::vector<std::string> cmd;
    cmd.push_back("ffprobe");
    cmd.push_back(argv[1]);
    int outFD = -1;
    Util::Procs::StartPiped("FFProbe", cmd, 0, 0, &outFD);
    FILE * outFile = fdopen( outFD, "r" );
    char * fileBuf = 0;
    size_t fileBufLen = 0;
    while ( !(feof(outFile) || ferror(outFile)) && (getline(&fileBuf, &fileBufLen, outFile) != -1)){
      std::string line = fileBuf;
      if (line.find("Input") != std::string::npos){
        std::string tmp = line.substr(line.find("'") + 1);
        fileSpecs["fileName"] = tmp.substr(0, tmp.find("'"));
      }
      if (line.find("Duration") != std::string::npos ){
        std::string tmp = line.substr(line.find(": ", line.find("Duration")) + 2);
        tmp = tmp.substr(0, tmp.find(","));
        fileSpecs["duration"] = tmp;
        int length = (((atoi(tmp.substr(0,2).c_str()) * 60) + atoi(tmp.substr(3,2).c_str())) * 60) + atoi(tmp.substr(6,2).c_str());
        fileSpecs["length"] = length;
        length *= 100;
        length += atoi(tmp.substr(9,2).c_str());
        fileSpecs["lastms"] = length * 10;
      }
      if (line.find("bitrate") != std::string::npos ){
        std::string tmp = line.substr(line.find(": ", line.find("bitrate")) + 2);
        fileSpecs["bps"] = atoi(tmp.substr(0, tmp.find(" ")).c_str()) * 128;
      }
      if (line.find("Stream") != std::string::npos ){
        std::string tmp = line.substr(line.find(" ", line.find("Stream")) + 1);
        int strmIdx = fileSpecs["streams"].size();
        int curPos = 0;
        fileSpecs["streams"][strmIdx]["name"] = tmp.substr(curPos, tmp.find(": ", curPos) - curPos);
        curPos = tmp.find(": ", curPos) + 2;

        //curPos = tmp.find("(", curPos) + 1;
        //fileSpecs["streams"][strmIdx]["language"] = tmp.substr(curPos, tmp.find(")", curPos) - curPos);
        //curPos = tmp.find(")", curPos) + 3;

        fileSpecs["streams"][strmIdx]["type"] = tmp.substr(curPos, tmp.find(":", curPos) - curPos);
        curPos = tmp.find(":", curPos) + 2;
        fileSpecs["streams"][strmIdx]["codec"] = tmp.substr(curPos, tmp.find(" ", curPos) - curPos);
        curPos = tmp.find(",", curPos) + 2;
        if (fileSpecs["streams"][strmIdx]["type"] == "Video"){
          fileSpecs["streams"][strmIdx]["encoding"] = tmp.substr(curPos, tmp.find(",", curPos) - curPos);
          curPos = tmp.find(",", curPos) + 2;
          fileSpecs["streams"][strmIdx]["width"] = atoi(tmp.substr(curPos, tmp.find("x", curPos) - curPos).c_str());
          curPos = tmp.find("x", curPos) + 1;
          fileSpecs["streams"][strmIdx]["height"] = atoi(tmp.substr(curPos, tmp.find(",", curPos) - curPos).c_str());
          curPos = tmp.find(",", curPos) + 2;
          fileSpecs["streams"][strmIdx]["bps"] = atoi(tmp.substr(curPos, tmp.find(" ", curPos) - curPos).c_str()) * 128;
          curPos = tmp.find(",", curPos) + 2;
          fileSpecs["streams"][strmIdx]["fpks"] = (int)(atof(tmp.substr(curPos, tmp.find(" ", curPos) - curPos).c_str()) * 1000);
          curPos = tmp.find(",", curPos) + 2;
          fileSpecs["streams"][strmIdx]["tbr"] = (int)(atof(tmp.substr(curPos, tmp.find(" ", curPos) - curPos).c_str()) + 0.5);
          curPos = tmp.find(",", curPos) + 2;
          fileSpecs["streams"][strmIdx]["tbn"] = atoi(tmp.substr(curPos, tmp.find(" ", curPos) - curPos).c_str());
          curPos = tmp.find(",", curPos) + 2;
          fileSpecs["streams"][strmIdx]["tbc"] = atoi(tmp.substr(curPos, tmp.find(" ", curPos) - curPos).c_str());
        }else if (fileSpecs["streams"][strmIdx]["type"] == "Audio"){
          fileSpecs["streams"][strmIdx]["samplerate"] = atoi(tmp.substr(curPos, tmp.find(" ", curPos) - curPos).c_str());
          curPos = tmp.find(",", curPos) + 2;
          if (tmp.substr(curPos, tmp.find(",", curPos) - curPos) == "stereo"){
            fileSpecs["streams"][strmIdx]["channels"] = 2;
          }else if (tmp.substr(curPos, tmp.find(",", curPos) - curPos) == "mono"){
            fileSpecs["streams"][strmIdx]["channels"] = 1;
          }else{
            fileSpecs["streams"][strmIdx]["channels"] = tmp.substr(curPos, tmp.find(",", curPos) - curPos);
          }
          curPos = tmp.find(",", curPos) + 2;
          fileSpecs["streams"][strmIdx]["samplewidth"] = tmp.substr(curPos, tmp.find(",", curPos) - curPos);
          curPos = tmp.find(",", curPos) + 2;
          fileSpecs["streams"][strmIdx]["bps"] = atoi(tmp.substr(curPos, tmp.find(" ", curPos) - curPos).c_str()) * 128;
        }
      }
    }
    fclose( outFile );
    printf( "%s\n", fileSpecs.toPrettyString().c_str() );
    return 0;
  }
}

int main(int argc, char* argv[]) {
  return Info::getInfo(argc, argv);
}
