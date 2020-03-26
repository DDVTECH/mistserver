#include "analyser_hls.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/timing.h>
#include <string.h>

void AnalyserHLS::init(Util::Config &conf){
  Analyser::init(conf);
  JSON::Value opt;
  opt["long"] = "reconstruct";
  opt["short"] = "R";
  opt["arg"] = "string";
  opt["default"] = "";
  opt["help"] = "Reconstruct TS file from HLS to the given filename";
  conf.addOption("reconstruct", opt);
  opt.null();
}

void AnalyserHLS::getParts(const std::string &body){
//INFO_MSG("body: %s", body.c_str());
Util::sleep(500);
  parts.clear();
  std::stringstream data(body);
  std::string line;
  uint64_t no = 0;
  float durat = 0;
  refreshAt = Util::bootSecs() + 10;
  while (data.good()){
    std::getline(data, line);
    if (line.size() && *line.rbegin() == '\r'){line.resize(line.size() - 1);}
    if (!line.size()){continue;}
    if (line[0] != '#'){
      if (line.find("m3u") != std::string::npos){
        root = root.link(line);
        INFO_MSG("Found a sub-playlist, re-targeting %s", root.getUrl().c_str());
        refreshAt = Util::bootSecs();
        return;
      }

      if((line.find(".ts") != std::string::npos) || (line.find(".mp4") != std::string::npos) ){
        if (!parsedPart || no > parsedPart) {
          HTTP::URL newURL = root.link(line);
          //INFO_MSG("Discovered #%llu: %s", no, newURL.getUrl().c_str());
          // INFO_MSG("Discovered #%llu: %s", no, newURL.getUrl().c_str());
          parts.push_back(HLSPart(newURL, no, durat));
        }
        ++no;
      }

    }else{
      if (line.substr(0, 8) == "#EXTINF:"){durat = atof(line.c_str() + 8) * 1000;}
      if (line.substr(0, 22) == "#EXT-X-MEDIA-SEQUENCE:"){no = atoll(line.c_str() + 22);}
      if (line.substr(0, 14) == "#EXT-X-ENDLIST"){refreshAt = 0;}
      if (line.substr(0, 22) == "#EXT-X-TARGETDURATION:" && refreshAt){
        refreshAt = Util::bootSecs() + atoll(line.c_str() + 22) / 2;
      }
    }
  }
}

/// Returns true if we either still have parts to download, or are still refreshing the playlist.
bool AnalyserHLS::isOpen(){
  return (*isActive) && (parts.size() || refreshAt);
}

void AnalyserHLS::stop(){
  parts.clear();
  refreshAt = 0;
}

AnalyserHLS::AnalyserHLS(Util::Config &conf) : Analyser(conf){
  if (conf.getString("reconstruct") != ""){
    reconstruct.open(conf.getString("reconstruct").c_str());
    if (reconstruct.good()){
      WARN_MSG("Will reconstruct to %s", conf.getString("reconstruct").c_str());
    }
  }
  hlsTime = 0;
  parsedPart = 0;
  refreshAt = Util::bootSecs();
  refreshPlaylist = true;
}

bool AnalyserHLS::open(const std::string & filename){
  if (filename == "-") {
    FAIL_MSG("input from stdin not supported");
    return false;
  }

  //INFO_MSG("opening %s", filename.c_str());
  root = HTTP::URL(filename);
  root = uri.getURI().link(filename);

  return Analyser::open(filename); 
}

bool AnalyserHLS::readPlaylist(std::string source){
  char * data;
  size_t s;

  std::string pl = root.link(source).getUrl();

  DONTEVEN_MSG("playlist open url: %s", pl.c_str());
  uri.open(pl.c_str());
  //uri.open(source);


  uri.readAll(data,s);
  //uri.readAll(*this);
  
//  INFO_MSG("read done, buf size: %llu", buffer.bytes(0xffffffff));
  
  std::string line;
//  data = buffer.copy(buffer.bytes(0xffffffff)).c_str();
//  line =  buffer.copy(buffer.bytes(0xffffffff));


  std::stringstream body(data);
  while (body.good()){
    std::getline(body, line);
    if (line.size() && *line.rbegin() == '\r'){line.resize(line.size() - 1);}
    if (!line.size()){continue;}
    if (line[0] != '#'){
      if (line.find("m3u") != std::string::npos){
        root = root.link(line);
        root = uri.getURI().link(line);
        MEDIUM_MSG("Found a sub-playlist, re-targeting %s", root.getUrl().c_str());
        
        //if subplaylist, read again
        uri.open(root.getUrl());
        uri.readAll(data,s);

        break;
      }
    }
  }

  getParts(data);
  return true;
}



bool AnalyserHLS::parsePacket(){
  while (isOpen()){
    char * pl;
    size_t s;

    std::string segmentUri;
    if (refreshAt && Util::bootSecs() >= refreshAt){
      if(!readPlaylist(uriSource.c_str())){
        FAIL_MSG("Could not refresh playlist!");
        return false;
      }
    }

    // If there are parts to download, get one.
    if (parts.size()){
      HLSPart part = *parts.begin();
      parts.pop_front();

      segmentUri = uri.getURI().link(part.uri.path).getUrl().c_str();
      segmentUri = part.uri.getUrl().c_str();
      uri.open(segmentUri);
      uri.readAll(pl,s);
      MEDIUM_MSG("reading file: %s, size: %d", segmentUri.c_str(), s);
      
      if(s % 188){
        FAIL_MSG("Expected a multiple of 188 bytes, received %d bytes", DL.data().size());
        return false;
      }

      parsedPart = part.no;
      hlsTime += part.dur;
      mediaTime = (uint64_t)hlsTime;
      if (reconstruct.good()){reconstruct << DL.data();}
      return true;
    }

    // Hm. I guess we had no parts to get.
    if (refreshAt && refreshAt > Util::bootSecs()){
      // We're getting a live stream. Let's wait and check again.
      uint64_t sleepSecs = (refreshAt - Util::bootSecs());
      INFO_MSG("Sleeping for %" PRIu64 " seconds", sleepSecs);
      Util::sleep(sleepSecs * 1000);
    }
    // The non-live case is already handled in isOpen()
  }
  return false;
}

void AnalyserHLS::dataCallback(const char *ptr, size_t size) {
  //INFO_MSG("hls callback, size: %d, totalbufsize: %llu", size, buffer.bytes(0xffffffff));
  buffer.append(ptr, size);
}
