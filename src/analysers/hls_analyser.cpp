/// \file hls_analyser.cpp
/// Contains the code for the HLS Analysing tool.

#include <mist/config.h>
#include <mist/timing.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <iostream>

class HLSPart {
  public:
    HLSPart(std::string u, unsigned int s, unsigned int d) {
      uri = u;
      start = s;
      dur = d;
    }
    std::string uri;
    unsigned int start;
    unsigned int dur;
};

std::deque<HLSPart> getParts(std::string & body, std::string & uri) {
  size_t slashPos = uri.rfind('/');
  std::string uri_prefix = uri.substr(0, slashPos + 1);
  std::deque<HLSPart> out;
  std::stringstream data(body);
  std::string line;
  unsigned int start = 0;
  unsigned int durat = 0;
  do {
    line = "";
    std::getline(data, line);
    if (line.size() && *line.rbegin() == '\r'){
      line.resize(line.size() - 1);
    }
    if (line != "") {
      if (line[0] != '#') {
        out.push_back(HLSPart(uri_prefix + line, start, durat));
        start += durat;
      } else {
        if (line.substr(0, 8) == "#EXTINF:") {
          durat = atof(line.substr(8).c_str()) * 1000;
        }
      }
    }
  } while (line != "");
  return out;
}

int main(int argc, char ** argv) {
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption("mode", JSON::fromString("{\"long\":\"mode\", \"arg\":\"string\", \"short\":\"m\", \"default\":\"analyse\", \"help\":\"What to do with the stream. Valid modes are 'analyse', 'validate', 'output'.\"}"));
  conf.addOption("url", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"URL to HLS stream index file to retrieve.\"}"));
  conf.addOption("abort", JSON::fromString("{\"long\":\"abort\", \"short\":\"a\", \"arg\":\"integer\", \"default\":-1, \"help\":\"Abort after this many seconds of downloading. Negative values mean unlimited, which is the default.\"}"));
  conf.parseArgs(argc, argv);
  conf.activate();

  unsigned int port = 80;
  std::string url = conf.getString("url");
  if (url.substr(0, 7) != "http://") {
    DEBUG_MSG(DLVL_FAIL, "The URL must start with http://");
    return -1;
  }
  url = url.substr(7);

  std::string server = url.substr(0, url.find('/'));
  url = url.substr(url.find('/'));

  if (server.find(':') != std::string::npos) {
    port = atoi(server.substr(server.find(':') + 1).c_str());
    server = server.substr(0, server.find(':'));
  }

  long long int startTime = Util::bootSecs();
  long long int abortTime = conf.getInteger("abort");

  std::deque<HLSPart> parts;
  Socket::Connection conn;

  std::string playlist = url;
  bool repeat = false;
  std::string lastDown = "";
  unsigned int pos = 0;
  bool output = (conf.getString("mode") == "output");

  do {
    repeat = false;
    while (url.size() > 4 && (url.find(".m3u") != std::string::npos || url.find(".m3u8") != std::string::npos)) {
      playlist = url;
      DEBUG_MSG(DLVL_DEVEL, "Retrieving playlist: %s", url.c_str());
      if (!conn) {
        conn = Socket::Connection(server, port, false);
      }
      HTTP::Parser H;
      H.url = url;
      H.SetHeader("Host", server + ":" + JSON::Value((long long)port).toString());
      H.SendRequest(conn);
      H.Clean();
      while (conn && (abortTime <= 0 || Util::bootSecs() < startTime + abortTime) && (!conn.spool() || !H.Read(conn))) {}
      parts = getParts(H.body, url);
      if (!parts.size()) {        
        DEBUG_MSG(DLVL_FAIL, "Playlist parsing error - cancelling. state: %s/%s body size: %u", conn ? "Conn" : "Disconn", (Util::bootSecs() < (startTime + abortTime))?"NoTimeout":"TimedOut", H.body.size());
        if (conf.getString("mode") == "validate") {
          long long int endTime = Util::bootSecs();
          std::cout << startTime << ", " << endTime << ", " << (endTime - startTime) << ", " << pos << std::endl;
        }
        return -1;
      }
      H.Clean();
      url = parts.begin()->uri;
    }

    if (lastDown != "") {
      while (parts.size() && parts.begin()->uri != lastDown) {
        parts.pop_front();
      }
      if (parts.size() < 2) {
        repeat = true;
        Util::sleep(1000);
        continue;
      }
      parts.pop_front();
    }

    unsigned int lastRepeat = 0;
    unsigned int numRepeat = 0;
    while (parts.size() > 0 && (abortTime <= 0 || Util::bootSecs() < startTime + abortTime)) {
      HLSPart part = *parts.begin();
      parts.pop_front();
      DEBUG_MSG(DLVL_DEVEL, "Retrieving segment: %s (%u-%u)", part.uri.c_str(), part.start, part.start + part.dur);
      if (!conn) {
        conn = Socket::Connection(server, port, false);
      }
      HTTP::Parser H;
      H.url = part.uri;
      H.SetHeader("Host", server + ":" + JSON::Value((long long)port).toString());
      H.SendRequest(conn);
      H.Clean();
      while (conn && (abortTime <= 0 || Util::bootSecs() < startTime + abortTime) && (!conn.spool() || !H.Read(conn))) {}

      if (H.GetHeader("Content-Length") != "") {
        if (H.body.size() != atoi(H.GetHeader("Content-Length").c_str())) {
          DEBUG_MSG(DLVL_FAIL, "Expected %s bytes of data, but only received %lu.", H.GetHeader("Content-Length").c_str(), H.body.size());
          if (lastRepeat != part.start || numRepeat < 500){
            DEBUG_MSG(DLVL_FAIL,"Retrying");
            if (lastRepeat != part.start){
              numRepeat = 0;
              lastRepeat = part.start;
            }else{
              numRepeat ++;
            }
            parts.push_front(part);
            Util::wait(1000);
            continue;
          }else{
            DEBUG_MSG(DLVL_FAIL,"Aborting further downloading");
            repeat = false;
            break;
          }
        }
      }
      if (H.body.size() % 188){
        DEBUG_MSG(DLVL_FAIL, "Expected a multiple of 188 bytes, received %d bytes", H.body.size());
        if (lastRepeat != part.start || numRepeat < 500){
          DEBUG_MSG(DLVL_FAIL,"Retrying");
          if (lastRepeat != part.start){
            numRepeat = 0;
            lastRepeat = part.start;
          }else{
            numRepeat ++;
          }
          parts.push_front(part);
          Util::wait(1000);
          continue;
        }else{
          DEBUG_MSG(DLVL_FAIL,"Aborting further downloading");
          repeat = false;
          break;
        }
      }
      pos = part.start + part.dur;
      if (conf.getString("mode") == "validate" && (Util::bootSecs()-startTime+5)*1000 < pos) {
        Util::wait(pos - (Util::bootSecs()-startTime+5)*1000);
      }
      lastDown = part.uri;
      if (output) {
        std::cout << H.body;
      }
      H.Clean();
    }
  } while (repeat);
  DEBUG_MSG(DLVL_INFO, "mode: %s", conf.getString("mode").c_str());
  if (conf.getString("mode") == "validate") {
    long long int endTime = Util::bootSecs();
    std::cout << startTime << ", " << endTime << ", " << (endTime - startTime) << ", " << pos << std::endl;
  }
  return 0;
}

