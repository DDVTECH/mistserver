
#include "analyser_dash.h"
#include <fstream>
#include <iostream>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
#include <mist/timing.h>
#include <set>

#define OTHER 0x00
#define VIDEO 0x01
#define AUDIO 0x02

// http://cattop:8080/dash/bunny/index.mpd
// http://balderlaptop:8080/dash/f+short.mp4/index.mpd
//
///\brief simple struct for storage of stream-specific data

StreamData tempSD; // temp global

bool getDelimBlock(std::string &data, std::string name, size_t &blockStart, size_t &blockEnd, std::string delim){
  size_t offset = data.find(name);
  if (offset == std::string::npos){
    return false; // name string not found.
  }
  // expected: delim character BEFORE blockstart.
  offset--;

  blockStart = data.find(delim, offset);
  offset = blockStart + 1; // skip single character!
  blockEnd = data.find(delim, offset);

  if (blockStart == std::string::npos || blockEnd == std::string::npos){
    return false; // no start/end quotes found
  }

  blockEnd++; // include delim
  return true;
}

bool getValueBlock(std::string &data, std::string name, size_t &blockStart, size_t &blockEnd, std::string delim){
  size_t offset = data.find(name);
  if (offset == std::string::npos){
    return false; // name string not found.
  }
  blockStart = data.find(delim, offset);
  blockStart++;        // clip off quote characters
  offset = blockStart; // skip single character!
  blockEnd = data.find(delim, offset);
  if (blockStart == std::string::npos || blockEnd == std::string::npos){
    return false; // no start/end quotes found
  }
  return true;
}

bool getString(std::string &data, std::string name, std::string &output){
  size_t blockStart = 0;
  size_t blockEnd = 0;

  if (!getValueBlock(data, name, blockStart, blockEnd, "\"")){
    return false; // could not find value in this data block.
  }
  output = data.substr(blockStart, (blockEnd - blockStart));
  return true;
}

bool getLong(std::string &data, std::string name, long &output){
  size_t blockStart, blockEnd;
  if (!getValueBlock(data, name, blockStart, blockEnd, "\"")){
    return false; // could not find value in this data block.
  }
  output = atol((data.substr(blockStart, (blockEnd - blockStart))).c_str());
  return true;
}

// block expecting separate name and /name occurence, or name and /> before another occurence of <.
bool getBlock(std::string &data, std::string name, int offset, size_t &blockStart, size_t &blockEnd){
  blockStart = data.find("<" + name + ">", offset);
  if (blockStart == std::string::npos){
    blockStart = data.find("<" + name + " ", offset); // this considers both valid situations <name> and <name bla="bla"/>
  }

  if (blockStart == std::string::npos){
    INFO_MSG("no block start found for name: %s at offset: %i", name.c_str(), offset);
    return false;
  }

  blockEnd = data.find("/" + name + ">", blockStart);
  if (blockEnd == std::string::npos){
    blockEnd = data.find("/>", blockStart);
    if (blockEnd == std::string::npos){
      INFO_MSG("no block end found.");
      return false;
    }
    size_t temp = data.find("<", blockStart + 1,
                            (blockEnd - blockStart - 1)); // the +1 is to avoid re-interpreting the starting < //TODO!!
    if (temp != std::string::npos){// all info is epxected between <name ... />
      FAIL_MSG("block start found before block end. offset: %lu block: %s", temp, data.c_str());
      return false;
    }
    blockEnd += 2; // position after />
  }else{
    blockEnd += name.size() + 2; // position after /name>
  }

  return true;
}

bool parseAdaptationSet(std::string &data, std::set<seekPos> &currentPos){
  size_t offset = 0;
  size_t blockStart, blockEnd;
  tempSD.trackType = OTHER;
  // get value: mimetype //todo: handle this!
  std::string mimeType;
  if (!getString(data, "mimeType",
                 mimeType)){// get first occurence of mimeType. --> this will break if multiple mimetypes
                              // should be read from this block because no offset is provided. solution:
                              // use this on a substring containing the desired information.
    FAIL_MSG("mimeType not found");
    return false;
  }

  INFO_MSG("mimeType: %s", mimeType.c_str()); // checked, OK

  if (mimeType.find("video") != std::string::npos){tempSD.trackType = VIDEO;}
  if (mimeType.find("audio") != std::string::npos){tempSD.trackType = AUDIO;}
  if (tempSD.trackType == OTHER){
    FAIL_MSG("no audio or video type found. giving up.");
    return false;
  }

  // find an ID within this adaptationSet block.
  if (!getBlock(data, (std::string) "Representation", offset, blockStart, blockEnd)){
    FAIL_MSG("Representation not found");
    return false;
  }

  // representation string

  std::string block = data.substr(blockStart, (blockEnd - blockStart));
  INFO_MSG("Representation block: %s", block.c_str());
  ///\todo check if block is not junk?

  if (!getLong(block, "id", tempSD.trackID)){
    FAIL_MSG("Representation id not found in block %s", block.c_str());
    return false;
  }
  INFO_MSG("Representation/id: %li", tempSD.trackID); // checked, OK

  offset = 0;
  // get values from SegmentTemplate
  if (!getBlock(data, (std::string) "SegmentTemplate", offset, blockStart, blockEnd)){
    FAIL_MSG("SegmentTemplate not found");
    return false;
  }
  block = data.substr(blockStart, (blockEnd - blockStart));

  getLong(block, "timescale", tempSD.timeScale);
  getString(block, "media", tempSD.media);
  getString(block, "initialization", tempSD.initialization);

  size_t tmpBlockStart = 0;
  size_t tmpBlockEnd = 0;
  if (!getDelimBlock(tempSD.media, "RepresentationID", tmpBlockStart, tmpBlockEnd, "$")){
    FAIL_MSG("Failed to find and replace $RepresentationID$ in %s", tempSD.media.c_str());
    return false;
  }
  tempSD.media.replace(tmpBlockStart, (tmpBlockEnd - tmpBlockStart), "%d");

  if (!getDelimBlock(tempSD.media, "Time", tmpBlockStart, tmpBlockEnd, "$")){
    FAIL_MSG("Failed to find and replace $Time$ in %s", tempSD.media.c_str());
    return false;
  }
  tempSD.media.replace(tmpBlockStart, (tmpBlockEnd - tmpBlockStart), "%d");

  if (!getDelimBlock(tempSD.initialization, "RepresentationID", tmpBlockStart, tmpBlockEnd, "$")){
    FAIL_MSG("Failed to find and replace $RepresentationID$ in %s", tempSD.initialization.c_str());
    return false;
  }
  tempSD.initialization.replace(tmpBlockStart, (tmpBlockEnd - tmpBlockStart), "%d");

  // get segment timeline block from within segment template:
  size_t blockOffset = 0; // offset should be 0 because this is a new block
  if (!getBlock(block, "SegmentTimeline", blockOffset, blockStart, blockEnd)){
    FAIL_MSG("SegmentTimeline block not found");
    return false;
  }

  std::string block2 = block.substr(blockStart,
                                    (blockEnd - blockStart)); // overwrites previous block (takes just the segmentTimeline part

  int numS = 0;
  offset = 0;
  long long unsigned int totalDuration = 0;
  long timeValue;
  while (1){
    if (!getBlock(block2, "S", offset, blockStart, blockEnd)){
      if (numS == 0){
        FAIL_MSG("no S found within SegmentTimeline");
        return false;
      }else{
        INFO_MSG("all S found within SegmentTimeline %i", numS);
        return true; // break;  //escape from while loop (to return true)
      }
    }
    numS++;
    // stuff S data into: currentPos
    // searching for t(start position)
    std::string sBlock = block2.substr(blockStart, (blockEnd - blockStart));
    if (getLong(sBlock, "t", timeValue)){
      totalDuration = timeValue; // reset totalDuration to value of t
    }
    if (!getLong(sBlock, "d", timeValue)){// expected duration in every S.
      FAIL_MSG("no d found within S");
      return false;
    }
    // stuff data with old value (start of block)
    seekPos thisPos;
    thisPos.trackType = tempSD.trackType;
    thisPos.trackID = tempSD.trackID;
    thisPos.adaptationSet = tempSD.adaptationSet;
    // thisPos.trackID=id;
    thisPos.seekTime = totalDuration; // previous total duration is start time of this S.
    thisPos.duration = timeValue;
    thisPos.timeScale = tempSD.timeScale;

    static char charBuf[512];
    snprintf(charBuf, 512, tempSD.media.c_str(), tempSD.trackID, totalDuration);
    thisPos.url.assign(charBuf);

    currentPos.insert(thisPos); // assumes insert copies all data in seekPos struct.
    totalDuration += timeValue; // update totalDuration
    offset = blockEnd; // blockEnd and blockStart are absolute values within string, offset is not relevant.
  }
  return true;
}

bool parseXML(std::string &body, std::set<seekPos> &currentPos, std::vector<StreamData> &streamData){
  // for all adaptation sets
  // representation ID
  int numAdaptationSet = 0;
  size_t currentOffset = 0;
  size_t adaptationSetStart;
  size_t adaptationSetEnd;

  while (getBlock(body, "AdaptationSet", currentOffset, adaptationSetStart, adaptationSetEnd)){
    tempSD.adaptationSet = numAdaptationSet;
    numAdaptationSet++;
    INFO_MSG("adaptationSet found. start: %lu end: %lu num: %lu ", adaptationSetStart,
             adaptationSetEnd, (adaptationSetEnd - adaptationSetStart));
    // get substring: from <adaptationSet... to /adaptationSet>
    std::string adaptationSet = body.substr(adaptationSetStart, (adaptationSetEnd - adaptationSetStart));
    // function was verified: output as expected.

    if (!parseAdaptationSet(adaptationSet, currentPos)){
      FAIL_MSG("parseAdaptationSet returned false."); // this also happens in the case of OTHER mimetype.
                                                      // in that case it might be desirable to continue
                                                      // searching for valid data instead of quitting.
      return false;
    }
    streamData.push_back(tempSD); // put temp values into adaptation set vector
    currentOffset = adaptationSetEnd; // the getblock function should make sure End is at the correct offset.
  }
  if (numAdaptationSet == 0){
    FAIL_MSG("no adaptationSet found.");
    return false;
  }
  INFO_MSG("all adaptation sets found. total: %i", numAdaptationSet);
  return true;
}

dashAnalyser::dashAnalyser(Util::Config conf) : analysers(conf){
  port = 80;
  url = conf.getString("url");

  if (url.substr(0, 7) != "http://"){
    FAIL_MSG("The URL must start with http://");
    // return -1;
    exit(1);
  }

  url = url.substr(7); // found problem if url is to short!! it gives out of range when entering http://meh.meh

  if ((url.find('/') == std::string::npos) || (url.find(".mpd") == std::string::npos)){
    std::cout << "incorrect url" << std::endl;
    mayExecute = false;
    return;
  }

  server = url.substr(0, url.find('/'));
  url = url.substr(url.find('/'));

  if (server.find(':') != std::string::npos){
    port = atoi(server.substr(server.find(':') + 1).c_str());
    server = server.substr(0, server.find(':'));
  }

  startTime = Util::bootSecs();
  abortTime = conf.getInteger("abort");

  conn.open(server, port, false);

  if (!conn.connected()){
    mayExecute = false;
    return;
  }

  // url:
  INFO_MSG("url %s server: %s port: %d", url.c_str(), server.c_str(), port);
  urlPrependStuff = url.substr(0, url.rfind("/") + 1);
  INFO_MSG("prepend stuff: %s", urlPrependStuff.c_str());
  if (!conn){conn.open(server, port, false);}

  pos = 0;
  HTTP::Parser H;
  H.url = url;
  H.SetHeader("Host", server + ":" + JSON::Value((long long)port).toString());
  H.SendRequest(conn);
  H.Clean();
  while (conn && (!conn.spool() || !H.Read(conn))){}
  H.BuildResponse();

  currentPos;
  streamData;

  if (!parseXML(H.body, currentPos, streamData)){
    FAIL_MSG("Manifest parsing failed. body: \n %s", H.body.c_str());
    if (conf.getString("mode") == "validate"){
      long long int endTime = Util::bootSecs();
      std::cout << startTime << ", " << endTime << ", " << (endTime - startTime) << ", " << pos << std::endl;
    }
    // return -1;
    exit(1);
  }

  H.Clean();
  INFO_MSG("*********");
  INFO_MSG("*SUMMARY*");
  INFO_MSG("*********");

  INFO_MSG("num streams: %lu", streamData.size());
  for (unsigned int i = 0; i < streamData.size(); i++){
    INFO_MSG("");
    INFO_MSG("ID in vector %d", i);
    INFO_MSG("trackID %ld", streamData[i].trackID);
    INFO_MSG("adaptationSet %d", streamData[i].adaptationSet);
    INFO_MSG("trackType (audio 0x02, video 0x01) %d", streamData[i].trackType);
    INFO_MSG("TimeScale %ld", streamData[i].timeScale);
    INFO_MSG("Media string %s", streamData[i].media.c_str());
    INFO_MSG("Init string %s", streamData[i].initialization.c_str());
  }

  INFO_MSG("");

  for (unsigned int i = 0; i < streamData.size(); i++){// get init url
    static char charBuf[512];
    snprintf(charBuf, 512, streamData[i].initialization.c_str(), streamData[i].trackID);
    streamData[i].initURL.assign(charBuf);
    INFO_MSG("init url for adaptationSet %d trackID %ld: %s ", streamData[i].adaptationSet,
             streamData[i].trackID, streamData[i].initURL.c_str());
  }
}

bool dashAnalyser::hasInput(){
  return currentPos.size();
}

bool dashAnalyser::packetReady(){
  return (abortTime <= 0 || Util::bootSecs() < startTime + abortTime) && (currentPos.size() > 0);
}

dashAnalyser::~dashAnalyser(){
  INFO_MSG("stopped");
}

int dashAnalyser::doAnalyse(){

  ///\todo match adaptation set and track id?
  int tempID = 0;
  for (unsigned int i = 0; i < streamData.size(); i++){
    if (streamData[i].trackID == currentPos.begin()->trackID &&
        streamData[i].adaptationSet == currentPos.begin()->adaptationSet)
      tempID = i;
  }
  if (!conn){conn.open(server, port, false);}
  HTTP::Parser H;
  H.url = urlPrependStuff;
  H.url.append(currentPos.begin()->url);
  INFO_MSG("Retrieving segment: %s (%llu-%llu)", H.url.c_str(), currentPos.begin()->seekTime,
           currentPos.begin()->seekTime + currentPos.begin()->duration);
  H.SetHeader("Host", server + ":" + JSON::Value((long long)port).toString()); // wut?
  H.SendRequest(conn);
  // TODO: get response?
  H.Clean();

  while (conn && (!conn.spool() || !H.Read(conn))){}// ehm...
  if (!H.body.size()){
    FAIL_MSG("No data downloaded from %s", H.url.c_str());
    return 0;
  }

  size_t beforeParse = H.body.size();
  MP4::Box mp4Data;
  bool mdatSeen = false;
  while (mp4Data.read(H.body)){
    if (mp4Data.isType("mdat")){mdatSeen = true;}
  }

  if (!mdatSeen){
    FAIL_MSG("No mdat present. Sadface. :-(");
    return 0;
  }
  if (H.body.size()){
    FAIL_MSG("%lu bytes left in body. Assuming horrible things...",
             H.body.size()); //,H.body.c_str());
    std::cerr << H.body << std::endl;
    if (beforeParse == H.body.size()){return 0;}
  }
  H.Clean();

  pos = 1000 * (currentPos.begin()->seekTime + currentPos.begin()->duration) / streamData[tempID].timeScale;

  if (conf.getString("mode") == "validate" && (Util::bootSecs() - startTime + 5) * 1000 < pos){
    Util::wait(pos - (Util::bootSecs() - startTime + 5) * 1000);
  }

  currentPos.erase(currentPos.begin());

  endTime = pos;
  return pos;
}

int main(int argc, char **argv){
  Util::Config conf = Util::Config(argv[0]);

  conf.addOption(
      "mode",
      JSON::fromString("{\"long\":\"mode\", \"arg\":\"string\", \"short\":\"m\", "
                       "\"default\":\"analyse\", \"help\":\"What to "
                       "do with the stream. Valid modes are 'analyse', 'validate', 'output'.\"}"));
  conf.addOption("url", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"URL to "
                                         "HLS stream index file to retrieve.\"}"));
  conf.addOption("abort",
                 JSON::fromString("{\"long\":\"abort\", \"short\":\"a\", \"arg\":\"integer\", "
                                  "\"default\":-1, \"help\":\"Abort after "
                                  "this many seconds of downloading. Negative values mean "
                                  "unlimited, which is the default.\"}"));

  conf.addOption("detail",
                 JSON::fromString("{\"long\":\"detail\", \"short\":\"D\", \"arg\":\"num\", "
                                  "\"default\":2, \"help\":\"Detail level of analysis. \"}"));

  conf.parseArgs(argc, argv);
  conf.activate();

  dashAnalyser A(conf);
  A.Run();

  return 0;
}

int main2(int argc, char **argv){
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption(
      "mode",
      JSON::fromString("{\"long\":\"mode\", \"arg\":\"string\", \"short\":\"m\", "
                       "\"default\":\"analyse\", \"help\":\"What to "
                       "do with the stream. Valid modes are 'analyse', 'validate', 'output'.\"}"));
  conf.addOption("url", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"URL to "
                                         "HLS stream index file to retrieve.\"}"));
  conf.addOption("abort",
                 JSON::fromString("{\"long\":\"abort\", \"short\":\"a\", \"arg\":\"integer\", "
                                  "\"default\":-1, \"help\":\"Abort after "
                                  "this many seconds of downloading. Negative values mean "
                                  "unlimited, which is the default.\"}"));
  conf.parseArgs(argc, argv);
  conf.activate();

  unsigned int port = 80;
  std::string url = conf.getString("url");

  if (url.substr(0, 7) != "http://"){
    FAIL_MSG("The URL must start with http://");
    return -1;
  }
  url = url.substr(7); // found problem if url is to short!! it gives out of range when entering http://meh.meh

  std::string server = url.substr(0, url.find('/'));
  url = url.substr(url.find('/'));

  if (server.find(':') != std::string::npos){
    port = atoi(server.substr(server.find(':') + 1).c_str());
    server = server.substr(0, server.find(':'));
  }

  long long int startTime = Util::bootSecs();
  long long int abortTime = conf.getInteger("abort");

  Socket::Connection conn(server, port, false);

  // url:
  INFO_MSG("url %s server: %s port: %d", url.c_str(), server.c_str(), port);
  std::string urlPrependStuff = url.substr(0, url.rfind("/") + 1);
  INFO_MSG("prepend stuff: %s", urlPrependStuff.c_str());
  if (!conn){conn.open(server, port, false);}
  unsigned int pos = 0;
  HTTP::Parser H;
  H.url = url;
  H.SetHeader("Host", server + ":" + JSON::Value((long long)port).toString());
  H.SendRequest(conn);
  H.Clean();
  while (conn && (!conn.spool() || !H.Read(conn))){}
  H.BuildResponse();

  std::set<seekPos> currentPos;
  std::vector<StreamData> streamData;

  if (!parseXML(H.body, currentPos, streamData)){
    FAIL_MSG("Manifest parsing failed. body: \n %s", H.body.c_str());
    if (conf.getString("mode") == "validate"){
      long long int endTime = Util::bootSecs();
      std::cout << startTime << ", " << endTime << ", " << (endTime - startTime) << ", " << pos << std::endl;
    }
    return -1;
  }

  H.Clean();
  INFO_MSG("*********");
  INFO_MSG("*SUMMARY*");
  INFO_MSG("*********");

  INFO_MSG("num streams: %lu", streamData.size());
  for (unsigned int i = 0; i < streamData.size(); i++){
    INFO_MSG("");
    INFO_MSG("ID in vector %d", i);
    INFO_MSG("trackID %ld", streamData[i].trackID);
    INFO_MSG("adaptationSet %d", streamData[i].adaptationSet);
    INFO_MSG("trackType (audio 0x02, video 0x01) %d", streamData[i].trackType);
    INFO_MSG("TimeScale %ld", streamData[i].timeScale);
    INFO_MSG("Media string %s", streamData[i].media.c_str());
    INFO_MSG("Init string %s", streamData[i].initialization.c_str());
  }

  INFO_MSG("");

  for (unsigned int i = 0; i < streamData.size(); i++){// get init url
    static char charBuf[512];
    snprintf(charBuf, 512, streamData[i].initialization.c_str(), streamData[i].trackID);
    streamData[i].initURL.assign(charBuf);
    INFO_MSG("init url for adaptationSet %d trackID %ld: %s ", streamData[i].adaptationSet,
             streamData[i].trackID, streamData[i].initURL.c_str());
  }

  while (currentPos.size() && (abortTime <= 0 || Util::bootSecs() < startTime + abortTime)){
    std::cout << "blaa" << std::endl;

    ///\todo match adaptation set and track id?
    int tempID = 0;
    for (unsigned int i = 0; i < streamData.size(); i++){
      if (streamData[i].trackID == currentPos.begin()->trackID &&
          streamData[i].adaptationSet == currentPos.begin()->adaptationSet)
        tempID = i;
    }
    if (!conn){conn.open(server, port, false);}
    HTTP::Parser H;
    H.url = urlPrependStuff;
    H.url.append(currentPos.begin()->url);
    INFO_MSG("Retrieving segment: %s (%llu-%llu)", H.url.c_str(), currentPos.begin()->seekTime,
             currentPos.begin()->seekTime + currentPos.begin()->duration);
    H.SetHeader("Host", server + ":" + JSON::Value((long long)port).toString()); // wut?
    H.SendRequest(conn);
    // TODO: get response?
    H.Clean();
    while (conn && (!conn.spool() || !H.Read(conn))){}// ehm...
    if (!H.body.size()){
      FAIL_MSG("No data downloaded from %s", H.url.c_str());
      break;
    }
    size_t beforeParse = H.body.size();
    MP4::Box mp4Data;
    bool mdatSeen = false;
    while (mp4Data.read(H.body)){
      if (mp4Data.isType("mdat")){mdatSeen = true;}
    }
    if (!mdatSeen){
      FAIL_MSG("No mdat present. Sadface. :-(");
      break;
    }
    if (H.body.size()){
      FAIL_MSG("%lu bytes left in body. Assuming horrible things...",
               H.body.size()); //,H.body.c_str());
      std::cerr << H.body << std::endl;
      if (beforeParse == H.body.size()){break;}
    }
    H.Clean();
    pos = 1000 * (currentPos.begin()->seekTime + currentPos.begin()->duration) / streamData[tempID].timeScale;

    if (conf.getString("mode") == "validate" && (Util::bootSecs() - startTime + 5) * 1000 < pos){
      Util::wait(pos - (Util::bootSecs() - startTime + 5) * 1000);
    }

    currentPos.erase(currentPos.begin());
  }

  if (conf.getString("mode") == "validate"){
    long long int endTime = Util::bootSecs();
    std::cout << startTime << ", " << endTime << ", " << (endTime - startTime) << ", " << pos << std::endl;
  }

  return 0;
}
