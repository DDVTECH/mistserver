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
  uri.userAgentOverride = APPIDENT " - Load Tester " + JSON::Value(getpid()).asString();
  root = uri.getURI().link(filename);
  return readPlaylist(root.getUrl());
}

bool AnalyserHLS::readPlaylist(std::string source){
  char * data;
  size_t s;
  std::string pl = root.link(source).getUrl();
  DONTEVEN_MSG("playlist open url: %s", pl.c_str());
  uri.open(pl);
  uri.readAll(data,s);
  mediaDown += s;
  if (!s){return false;}

  std::string line;
  std::stringstream body(data);
  while (body.good()){
    std::getline(body, line);
    if (line.size() && *line.rbegin() == '\r'){line.resize(line.size() - 1);}
    if (!line.size()){continue;}
    if (line[0] != '#'){
      if (line.find("m3u") != std::string::npos){
        root = root.link(line);
        MEDIUM_MSG("Found a sub-playlist, re-targeting %s", root.getUrl().c_str());
        return readPlaylist(root.getUrl());
      }
    }
  }
  getParts(data);
  return true;
}

void AnalyserHLS::getParts(const std::string &body){
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
      if((line.find(".ts") != std::string::npos) || (line.find(".mp4") != std::string::npos) ){
        if (!parsedPart || no > parsedPart) {
          HTTP::URL newURL = root.link(line);
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


bool AnalyserHLS::parsePacket(){
  while (isOpen()){
    // If there are parts to download, get one.
    if (parts.size()){
      HLSPart part = *parts.begin();
      parts.pop_front();

      char * pl;
      size_t s;
      uint64_t micros = Util::getMicros();
      uri.open(part.uri);
      uri.readAll(pl,s);
      mediaDown += s;
      micros = Util::getMicros(micros);
      if (part.dur < micros / 1000){
        WARN_MSG("Downloading segment %s took %" PRIu64 "ms but has a duration of %zums", part.uri.getUrl().c_str(), micros/1000,(size_t)part.dur);
      }
      MEDIUM_MSG("reading file: %s, size: %zu", part.uri.getUrl().c_str(), s);
      
      if(s % 188){
        FAIL_MSG("Expected a multiple of 188 bytes, received %zu bytes", s);
        return false;
      }

      parsedPart = part.no;
      hlsTime += part.dur;
      mediaTime = (uint64_t)hlsTime;
      if (reconstruct.good()){reconstruct.write(pl, s);}
      return true;
    }

    //Refresh playlist if needed
    if (refreshAt && Util::bootSecs() >= refreshAt){
      if(!readPlaylist(root.getUrl())){
        FAIL_MSG("Could not refresh playlist!");
        return false;
      }
    }

    // Hm. I guess we had no parts to get, and no playlist to refresh.
    if (refreshAt && refreshAt > Util::bootSecs()){
      // We're getting a live stream. Let's wait and check again.
      uint64_t sleepSecs = (refreshAt - Util::bootSecs());
      HIGH_MSG("Sleeping for %" PRIu64 " seconds", sleepSecs);
      Util::sleep(sleepSecs * 1000);
    }
    // The non-live case is already handled in isOpen()
  }
  return false;
}

