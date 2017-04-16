#include "analyser_hls.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/timing.h>
#include <string.h>
#include <sys/sysinfo.h>

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
      if (!parsedPart || no > parsedPart){
        HTTP::URL newURL = root.link(line);
        INFO_MSG("Discovered: %s", newURL.getUrl().c_str());
        parts.push_back(HLSPart(newURL, no, durat));
      }
      ++no;
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

bool AnalyserHLS::open(const std::string &url){
  root = HTTP::URL(url);
  if (root.protocol != "http"){
    FAIL_MSG("Only http protocol is supported (%s not supported)", root.protocol.c_str());
    return false;
  }
  return true;
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
}

/// Downloads the given URL into 'H', returns true on success.
/// Makes at most 5 attempts, and will wait no longer than 5 seconds without receiving data.
bool AnalyserHLS::download(const HTTP::URL &link){
  if (!link.host.size()){return false;}
  INFO_MSG("Retrieving %s", link.getUrl().c_str());
  unsigned int loop = 6; // max 5 attempts
  while (--loop){// loop while we are unsuccessful
    H.Clean();
    // Reconnect if needed
    if (!conn || link.host != connectedHost || link.getPort() != connectedPort){
      conn.close();
      connectedHost = link.host;
      connectedPort = link.getPort();
      conn = Socket::Connection(connectedHost, connectedPort, true);
    }
    H.url = "/" + link.path;
    if (link.port.size()){
      H.SetHeader("Host", link.host + ":" + link.port);
    }else{
      H.SetHeader("Host", link.host);
    }
    H.SendRequest(conn);
    H.Clean();
    uint64_t reqTime = Util::bootSecs();
    while (conn && Util::bootSecs() < reqTime + 5){
      // No data? Wait for a second or so.
      if (!conn.spool()){
        Util::sleep(1000);
        continue;
      }
      // Data! Check if we can parse it...
      if (H.Read(conn)){
        return true; // Success!
      }
      // reset the 5 second timeout
      reqTime = Util::bootSecs();
    }
    if (conn){
      FAIL_MSG("Timeout while retrieving %s", link.getUrl().c_str());
      return false;
    }
    Util::sleep(500); // wait a bit before retrying
  }
  FAIL_MSG("Could not retrieve %s", link.getUrl().c_str());
  return false;
}

bool AnalyserHLS::parsePacket(){
  while (isOpen()){
    // If needed, refresh the playlist
    if (refreshAt && Util::bootSecs() >= refreshAt){
      if (download(root)){
        getParts(H.body);
      }else{
        FAIL_MSG("Could not refresh playlist!");
        return false;
      }
    }

    // If there are parts to download, get one.
    if (parts.size()){
      HLSPart part = *parts.begin();
      parts.pop_front();
      if (!download(part.uri)){return false;}
      if (H.GetHeader("Content-Length") != ""){
        if (H.body.size() != atoi(H.GetHeader("Content-Length").c_str())){
          FAIL_MSG("Expected %s bytes of data, but only received %lu.",
                   H.GetHeader("Content-Length").c_str(), H.body.size());
          return false;
        }
      }
      if (H.body.size() % 188){
        FAIL_MSG("Expected a multiple of 188 bytes, received %d bytes", H.body.size());
        return false;
      }
      parsedPart = part.no;
      hlsTime += part.dur;
      mediaTime = (uint64_t)hlsTime;
      if (reconstruct.good()){reconstruct << H.body;}
      H.Clean();
      return true;
    }

    // Hm. I guess we had no parts to get.
    if (refreshAt && refreshAt > Util::bootSecs()){
      // We're getting a live stream. Let's wait and check again.
      uint32_t sleepSecs = (refreshAt - Util::bootSecs());
      INFO_MSG("Sleeping for %lu seconds", sleepSecs);
      Util::sleep(sleepSecs * 1000);
    }
    //The non-live case is already handled in isOpen()
  }
  return false;
}

