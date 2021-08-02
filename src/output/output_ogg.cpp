#include "output_ogg.h"
#include <algorithm>
#include <mist/bitfields.h>
#include <mist/bitstream.h>
#include <mist/defines.h>

namespace Mist{
  OutOGG::OutOGG(Socket::Connection &conn) : HTTPOutput(conn){realTime = 0;}

  OutOGG::~OutOGG(){}

  void OutOGG::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "OGG";
    capa["friendly"] = "OGG over HTTP";
    capa["desc"] = "Pseudostreaming in OGG format over HTTP";
    capa["deps"] = "HTTP";
    capa["url_rel"] = "/$.ogg";
    capa["url_match"] = "/$.ogg";
    capa["codecs"][0u][0u].append("theora");
    capa["codecs"][0u][1u].append("vorbis");
    capa["codecs"][0u][1u].append("opus");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/ogg";
    capa["methods"][0u]["hrn"] = "OGG progressive";
    capa["methods"][0u]["priority"] = 8u;
    capa["methods"][0u]["nolive"] = 1;
  }

  void OutOGG::sendNext(){

    OGG::oggSegment newSegment;
    thisPacket.getString("data", newSegment.dataString);
    pageBuffer[thisIdx].totalFrames = ((double)thisPacket.getTime() / (1000000.0f / M.getFpks(thisIdx))) +
                                      1.5; // should start at 1. added .5 for rounding.

    if (pageBuffer[thisIdx].codec == OGG::THEORA){
      newSegment.isKeyframe = thisPacket.getFlag("keyframe");
      if (newSegment.isKeyframe == true){
        pageBuffer[thisIdx].sendTo(myConn); // send data remaining in buffer (expected to fit on a
                                            // page), keyframe will allways start on new page
        pageBuffer[thisIdx].lastKeyFrame = pageBuffer[thisIdx].totalFrames;
      }
      newSegment.framesSinceKeyFrame = pageBuffer[thisIdx].totalFrames - pageBuffer[thisIdx].lastKeyFrame;
      newSegment.lastKeyFrameSeen = pageBuffer[thisIdx].lastKeyFrame;
    }

    newSegment.frameNumber = pageBuffer[thisIdx].totalFrames;
    newSegment.timeStamp = thisPacket.getTime();

    pageBuffer[thisIdx].oggSegments.push_back(newSegment);

    if (pageBuffer[thisIdx].codec == OGG::VORBIS){
      pageBuffer[thisIdx].vorbisStuff(); // this updates lastKeyFrame
    }
    while (pageBuffer[thisIdx].shouldSend()){pageBuffer[thisIdx].sendTo(myConn);}
  }

  bool OutOGG::onFinish(){
    for (std::map<size_t, OGG::Page>::iterator it = pageBuffer.begin(); it != pageBuffer.end(); it++){
      it->second.setHeaderType(OGG::EndOfStream);
      it->second.sendTo(myConn);
    }
    return false;
  }
  bool OutOGG::parseInit(const std::string &initData, std::deque<std::string> &output){
    size_t index = 0;
    if (initData[0] == 0x02){//"special" case, requires interpretation similar to table
      if (initData.size() < 7){
        FAIL_MSG("initData size too tiny (size: %zu)", initData.size());
        return false;
      }
      size_t len1 = 0;
      size_t len2 = 0;
      index = 1;
      while (initData[index] == 255){// get len 1
        len1 += initData[index++];
      }
      len1 += initData[index++];

      while (initData[index] == 255){// get len 1
        len2 += initData[index++];
      }
      len2 += initData[index++];

      if (initData.size() < (len1 + len2 + 4)){
        FAIL_MSG("initData size too tiny (size: %zu)", initData.size());
        return false;
      }

      output.push_back(initData.substr(index, len1));
      index += len1;
      output.push_back(initData.substr(index, len2));
      index += len2;
      output.push_back(initData.substr(index));
    }else{
      if (initData.size() < 7){
        FAIL_MSG("initData size too tiny (size: %zu)", initData.size());
        return false;
      }
      unsigned int len = 0;
      for (unsigned int i = 0; i < 3; i++){
        std::string temp = initData.substr(index, 2);
        len = Bit::btohs(temp.data());
        index += 2; // start of data
        if (index + len > initData.size()){
          FAIL_MSG("index+len > initData size");
          return false;
        }
        output.push_back(initData.substr(index, len)); // add data to output deque
        index += len;
        INFO_MSG("init data len[%d]: %d ", i, len);
      }
    }

    return true;
  }

  void OutOGG::sendHeader(){
    HTTP_S.Clean(); // make sure no parts of old requests are left in any buffers
    HTTP_S.SetHeader("Content-Type", "video/ogg");
    HTTP_S.protocol = "HTTP/1.0";
    myConn.SendNow(HTTP_S.BuildResponse("200",
                                        "OK")); // no SetBody = unknown length - this is intentional, we will stream the entire file

    std::map<size_t, std::deque<std::string> > initData;

    OGG::oggSegment newSegment;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      if (M.getCodec(it->first) == "theora"){// get size and position of init data for this page.
        parseInit(M.getInit(it->first), initData[it->first]);
        pageBuffer[it->first].codec = OGG::THEORA;
        pageBuffer[it->first].totalFrames =
            1; // starts at frame number 1, according to weird offDetectMeta function.
        std::string tempStr = initData[it->first][0];
        theora::header tempHead((char *)tempStr.c_str(), 42);
        pageBuffer[it->first].split = tempHead.getKFGShift();
        INFO_MSG("got theora KFG shift: %d", pageBuffer[it->first].split); // looks OK.
      }else if (M.getCodec(it->first) == "vorbis"){
        parseInit(M.getInit(it->first), initData[it->first]);
        pageBuffer[it->first].codec = OGG::VORBIS;
        pageBuffer[it->first].totalFrames = 0;
        pageBuffer[it->first].sampleRate = M.getRate(it->first);
        pageBuffer[it->first].prevBlockFlag = -1;
        vorbis::header tempHead((char *)initData[it->first][0].data(), initData[it->first][0].size());
        pageBuffer[it->first].blockSize[0] = std::min(tempHead.getBlockSize0(), tempHead.getBlockSize1());
        pageBuffer[it->first].blockSize[1] = std::max(tempHead.getBlockSize0(), tempHead.getBlockSize1());
        char audioChannels = tempHead.getAudioChannels(); //?
        vorbis::header tempHead2((char *)initData[it->first][2].data(), initData[it->first][2].size());
        pageBuffer[it->first].vorbisModes = tempHead2.readModeDeque(audioChannels); // getting modes
      }else if (M.getCodec(it->first) == "opus"){
        pageBuffer[it->first].totalFrames = 0; //?
        pageBuffer[it->first].codec = OGG::OPUS;
        initData[it->first].push_back(M.getInit(it->first));
        initData[it->first].push_back(
            std::string("OpusTags\000\000\000\012MistServer\000\000\000\000", 26));
      }
      pageBuffer[it->first].clear(OGG::BeginOfStream, 0, it->first,
                                  0); // CREATES a (map)pageBuffer object, *it = id, pagetype=BOS
      newSegment.dataString = initData[it->first].front();
      initData[it->first].pop_front();
      pageBuffer[it->first].oggSegments.push_back(newSegment);
      pageBuffer[it->first].sendTo(myConn, 0); // granule position of 0
    }
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      while (initData[it->first].size()){
        newSegment.dataString = initData[it->first].front();
        initData[it->first].pop_front();
        pageBuffer[it->first].oggSegments.push_back(newSegment);
      }
      while (pageBuffer[it->first].oggSegments.size()){
        pageBuffer[it->first].sendTo(myConn, 0); // granule position of 0
      }
    }
    sentHeader = true;
  }

  void OutOGG::onRequest(){
    if (HTTP_R.Read(myConn)){
      DEVEL_MSG("Received request %s", HTTP_R.getUrl().c_str());

      if (HTTP_R.method == "OPTIONS" || HTTP_R.method == "HEAD"){
        HTTP_S.Clean();
        HTTP_S.SetHeader("Content-Type", "video/ogg");
        HTTP_S.protocol = "HTTP/1.0";
        HTTP_S.SendResponse("200", "OK", myConn);
        HTTP_S.Clean();
        return;
      }

      if (HTTP_R.GetVar("audio") != ""){
        size_t track = atoll(HTTP_R.GetVar("audio").c_str());
        userSelect[track].reload(streamName, track);
      }
      if (HTTP_R.GetVar("video") != ""){
        size_t track = atoll(HTTP_R.GetVar("video").c_str());
        userSelect[track].reload(streamName, track);
      }
      parseData = true;
      wantRequest = false;
      HTTP_R.Clean();
    }
  }
}// namespace Mist
