#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sstream>

#include "timing.h"
#include "converter.h"
#include "procs.h"
#include "config.h"

namespace Converter {
  
  Converter::Converter(){
    fillFFMpegEncoders();
  }
  
  void Converter::fillFFMpegEncoders(){
    std::vector<char*> cmd;
    cmd.reserve(3);
    cmd.push_back((char*)"ffmpeg");
    cmd.push_back((char*)"-encoders");
    cmd.push_back(NULL);
    int outFD = -1;
    Util::Procs::StartPiped("FFMpegInfo", &cmd[0], 0, &outFD, 0);
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
    JSON::Value result;
    for (converterInfo::iterator convIt = allCodecs.begin(); convIt != allCodecs.end(); convIt++){
      for (codecInfo::iterator codIt = convIt->second.begin(); codIt != convIt->second.end(); codIt++){
        result[convIt->first][codIt->first] = codIt->second;
      }
    }
    return result;
  }
  
  JSON::Value Converter::queryPath(std::string myPath){
    std::vector<char*> cmd;
    cmd.reserve(3);
    std::string mistPath = Util::getMyPath() + "MistInfo";
    cmd.push_back((char*)mistPath.c_str());
    cmd.push_back(NULL);
    cmd.push_back(NULL);
    fprintf( stderr, "Querying %s\n", myPath.c_str());
    JSON::Value result;
    DIR * Dirp = opendir(myPath.c_str());
    struct stat StatBuf;
    if (Dirp){
      dirent * entry;
      while ((entry = readdir(Dirp))){
        if (stat(std::string(myPath + "/" + entry->d_name).c_str(), &StatBuf) == -1){
          continue;
        }
        if ((StatBuf.st_mode & S_IFREG) == 0){
          continue;
        }
        std::string fileName = entry->d_name;
        std::string myPath = std::string(myPath + (myPath[myPath.size()-1] == '/' ? "" : "/") +  entry->d_name);
        cmd[1] = (char*)myPath.c_str();
        int outFD = -1;
        Util::Procs::StartPiped("MistInfo", &cmd[0], 0, &outFD, 0);
        while( Util::Procs::isActive("MistInfo")){ Util::sleep(10); }
        FILE * outFile = fdopen( outFD, "r" );
        char * fileBuf = 0;
        size_t fileBufLen = 0;
        getline(&fileBuf, &fileBufLen, outFile);
        std::string line = fileBuf;
        result[fileName] = JSON::fromString(std::string(fileBuf));
        if ( !result[fileName]){
          result.removeMember(fileName);
        }
        fclose( outFile );
      }
    }
    return result;
  }

  void Converter::startConversion(std::string name, JSON::Value parameters) {
    if ( !parameters.isMember("input")){
      statusHistory[name] = "No input file supplied";
      return;
    }
    if ( !parameters.isMember("output")){
      statusHistory[name] = "No output file supplied";
      return;
    }
    if ( !parameters.isMember("encoder")){
      statusHistory[name] = "No encoder specified";
      return;
    }
    if (allCodecs.find(parameters["encoder"]) == allCodecs.end()){
      statusHistory[name] = "Can not find encoder " + parameters["encoder"].asString();
      return;
    }
    if (parameters.isMember("video")){
      if (parameters["video"].isMember("width") && !parameters["video"].isMember("height")){
        statusHistory[name] = "No height parameter given";
        return;
      }
      if (parameters["video"].isMember("height") && !parameters["video"].isMember("width")){
        statusHistory[name] = "No width parameter given";
        return;
      }
    }
    std::stringstream encoderCommand;
    if (parameters["encoder"] == "ffmpeg"){
      encoderCommand << "ffmpeg -i ";
      encoderCommand << parameters["input"].asString() << " ";
      if (parameters.isMember("video")){
        if ( !parameters["video"].isMember("codec") || parameters["video"]["codec"] == "copy"){
          encoderCommand << "-vcodec copy ";
        }else{
          codecInfo::iterator vidCodec = allCodecs["ffmpeg"].find(parameters["video"]["codec"]);
          if (vidCodec == allCodecs["ffmpeg"].end()){
            statusHistory[name] = "Can not find video codec " + parameters["video"]["codec"].asString();
            return;
          }
          encoderCommand << "-vcodec " << vidCodec->first << " ";
          if (parameters["video"].isMember("fpks")){
            encoderCommand << "-r " << parameters["video"]["fpks"].asInt() / 1000 << " "; 
          }
          if (parameters["video"].isMember("width")){
            encoderCommand << "-s " << parameters["video"]["width"].asInt() << "x" << parameters["video"]["height"].asInt() << " ";
          }
          ///\todo Keyframe interval (different in older and newer versions of ffmpeg?)
        }
      }else{
        encoderCommand << "-vn ";
      }
      if (parameters.isMember("audio")){
        if ( !parameters["audio"].isMember("codec")){
          encoderCommand << "-acodec copy ";
        }else{
          codecInfo::iterator audCodec = allCodecs["ffmpeg"].find(parameters["audio"]["codec"]);
          if (audCodec == allCodecs["ffmpeg"].end()){
            statusHistory[name] = "Can not find audio codec " + parameters["audio"]["codec"].asString();
            return;
          }
          if (audCodec->second == "aac"){
            encoderCommand << "-strict -2 ";
          }
          encoderCommand << "-acodec " << audCodec->first << " ";
          if (parameters["audio"].isMember("samplerate")){
            encoderCommand << "-ar " << parameters["audio"]["samplerate"].asInt() << " ";
          }
        }
      }else{
        encoderCommand << "-an ";
      }
      encoderCommand << "-f flv -";
    }
    int statusFD = -1;
    Util::Procs::StartPiped2(name,encoderCommand.str(),Util::getMyPath() + "MistFLV2DTSC -o " + parameters["output"].asString(),0,0,&statusFD,0);
    parameters["statusFD"] = statusFD;
    allConversions[name] = parameters;
  }
  
  void Converter::updateStatus(){
    if (allConversions.size()){
      std::map<std::string,JSON::Value>::iterator cIt;
      bool hasChanged = true;
      while (hasChanged && allConversions.size()){
        hasChanged = false;
        for (cIt = allConversions.begin(); cIt != allConversions.end(); cIt++){
          if (Util::Procs::isActive(cIt->first)){
            int statusFD = dup(cIt->second["statusFD"].asInt());
            fsync( statusFD );
            FILE* statusFile = fdopen( statusFD, "r" );
            char * fileBuf = 0;
            size_t fileBufLen = 0;
            fseek(statusFile,0,SEEK_END);
            std::string line;
            int totalTime = 0;
            do{
              getdelim(&fileBuf, &fileBufLen, '\r', statusFile);
              line = fileBuf;
              if (line.find("Duration") != std::string::npos){
                int curOffset = line.find("Duration: ") + 10;
                totalTime += atoi(line.substr(curOffset, 2).c_str()) * 60 * 60 * 1000;
                totalTime += atoi(line.substr(curOffset+3, 2).c_str()) * 60 * 1000;
                totalTime += atoi(line.substr(curOffset+6, 2).c_str()) *1000;
                totalTime += atoi(line.substr(curOffset+9, 2).c_str()) * 10;
                cIt->second["duration"] = totalTime;
              }
            }while ( !feof(statusFile) && line.find("frame") != 0);//"frame" is the fist word on an actual status line of ffmpeg
            if ( !feof(statusFile)){
              cIt->second["status"] = parseFFMpegStatus( line );
              cIt->second["status"]["duration"] = cIt->second["duration"];
              cIt->second["status"]["progress"] = (cIt->second["status"]["time"].asInt() * 100) / cIt->second["duration"].asInt();
            }else{
              line.erase(line.end()-1);
              line = line.substr( line.rfind("\n") + 1 );
              cIt->second["status"] = line;
            }
            free(fileBuf);
            fclose(statusFile);
          }else{
            if (statusHistory.find( cIt->first ) == statusHistory.end()){
              statusHistory[cIt->first] = "Conversion successful, running DTSCFix";
              Util::Procs::Start(cIt->first+"DTSCFix",Util::getMyPath() + "MistDTSCFix " + cIt->second["output"].asString());
            }
            allConversions.erase(cIt);
            hasChanged = true;
          }
          break;
        }
      }
    }
    if(statusHistory.size()){
      std::map<std::string,std::string>::iterator sIt;
      for (sIt = statusHistory.begin(); sIt != statusHistory.end(); sIt++){
        if (statusHistory[sIt->first].find("DTSCFix") != std::string::npos){
          if (Util::Procs::isActive(sIt->first+"DTSCFIX")){
            continue;
          }
          statusHistory[sIt->first] = "Conversion successful";
        }
      }
    }
  }
  
  JSON::Value Converter::parseFFMpegStatus(std::string statusLine){
    JSON::Value result;
    int curOffset = statusLine.find("frame=") + 6;
    result["frame"] = atoi(statusLine.substr(curOffset, statusLine.find("fps=") - curOffset).c_str() );
    curOffset = statusLine.find("time=") + 5;
    int myTime = 0;
    myTime += atoi(statusLine.substr(curOffset, 2).c_str()) * 60 * 60 * 1000;
    myTime += atoi(statusLine.substr(curOffset+3, 2).c_str()) * 60 * 1000;
    myTime += atoi(statusLine.substr(curOffset+6, 2).c_str()) *1000;
    myTime += atoi(statusLine.substr(curOffset+9, 2).c_str()) * 10;
    result["time"] = myTime;
    return result;
  }
  
  JSON::Value Converter::getStatus(){
    updateStatus();
    JSON::Value result;
    if (allConversions.size()){
      for (std::map<std::string,JSON::Value>::iterator cIt = allConversions.begin(); cIt != allConversions.end(); cIt++){
        result[cIt->first] = cIt->second["status"];
      }
    }
    if (statusHistory.size()){
      std::map<std::string,std::string>::iterator sIt;
      for (sIt = statusHistory.begin(); sIt != statusHistory.end(); sIt++){
        result[sIt->first] = sIt->second;
      }
    }
    return result;
  }
  
  void Converter::clearStatus(){
    statusHistory.clear();
  }
}
