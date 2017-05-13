#include "output_progressive_ogg.h"
#include <mist/bitstream.h>
#include <mist/defines.h>
#include <algorithm>

namespace Mist {
  OutProgressiveOGG::OutProgressiveOGG(Socket::Connection & conn) : HTTPOutput(conn){
    realTime = 0;
  }

  OutProgressiveOGG::~OutProgressiveOGG(){}

  void OutProgressiveOGG::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "OGG";
    capa["desc"] = "Enables HTTP protocol progressive streaming.";
    capa["deps"] = "HTTP";
    capa["url_rel"] = "/$.ogg";
    capa["url_match"] = "/$.ogg";
    capa["codecs"][0u][0u].append("theora");
    capa["codecs"][0u][1u].append("vorbis");
    capa["codecs"][0u][1u].append("opus");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/ogg";
    capa["methods"][0u]["priority"] = 8ll;
    capa["methods"][0u]["nolive"] = 1;
  }

  void OutProgressiveOGG::sendNext(){
    unsigned int track = thisPacket.getTrackId();


    OGG::oggSegment newSegment;
    thisPacket.getString("data", newSegment.dataString);
    pageBuffer[track].totalFrames = ((double)thisPacket.getTime() / (1000000.0f / myMeta.tracks[track].fpks)) + 1.5; //should start at 1. added .5 for rounding.

    if (pageBuffer[track].codec == OGG::THEORA){
      newSegment.isKeyframe = thisPacket.getFlag("keyframe");
      if (newSegment.isKeyframe == true){
        pageBuffer[track].sendTo(myConn);//send data remaining in buffer (expected to fit on a page), keyframe will allways start on new page
        pageBuffer[track].lastKeyFrame = pageBuffer[track].totalFrames;
      }
      newSegment.framesSinceKeyFrame = pageBuffer[track].totalFrames - pageBuffer[track].lastKeyFrame;
      newSegment.lastKeyFrameSeen = pageBuffer[track].lastKeyFrame;
    }

    newSegment.frameNumber = pageBuffer[track].totalFrames;
    newSegment.timeStamp = thisPacket.getTime();

    pageBuffer[track].oggSegments.push_back(newSegment);

    if (pageBuffer[track].codec == OGG::VORBIS){
      pageBuffer[track].vorbisStuff();//this updates lastKeyFrame
    }
    while (pageBuffer[track].shouldSend()){ 
      pageBuffer[track].sendTo(myConn);
    }
  }

  bool OutProgressiveOGG::onFinish(){
    for (std::map<long long unsigned int, OGG::Page>::iterator it = pageBuffer.begin(); it != pageBuffer.end(); it++){
      it->second.setHeaderType(OGG::EndOfStream);
      it->second.sendTo(myConn);
    }
    return false;
  }
  bool OutProgressiveOGG::parseInit(std::string & initData, std::deque<std::string> & output){
    std::string temp;
    unsigned int index = 0;
    if (initData[0] == 0x02){ //"special" case, requires interpretation similar to table
      if (initData.size() < 7){
        FAIL_MSG("initData size too tiny (size: %lu)", initData.size());
        return false;
      }
      unsigned int len1 = 0 ;
      unsigned int len2 = 0 ;
      index = 1;
      while (initData[index] == 255){ //get len 1
        len1 += initData[index++];
      }
      len1 += initData[index++];

      while (initData[index] == 255){ //get len 1
        len2 += initData[index++];
      }
      len2 += initData[index++];

      if (initData.size() < (len1 + len2 + 4)){
        FAIL_MSG("initData size too tiny (size: %lu)", initData.size());
        return false;
      }

      temp = initData.substr(index, len1);
      output.push_back(temp);
      index += len1;
      temp = initData.substr(index, len2);
      output.push_back(temp);
      index += len2;
      temp = initData.substr(index);      //remainder of string:
      output.push_back(temp);             //add data to output deque
    } else {
      if (initData.size() < 7){
        FAIL_MSG("initData size too tiny (size: %lu)", initData.size());
        return false;
      }
      unsigned int len = 0;
      for (unsigned int i = 0; i < 3; i++){
        temp = initData.substr(index, 2);
        len = (((unsigned int)temp[0]) << 8) | (temp[1]); //2 bytes len
        index += 2; //start of data
        if (index + len > initData.size()){
          FAIL_MSG("index+len > initData size");
          return false;
        }
        temp = initData.substr(index, len);
        output.push_back(temp);             //add data to output deque
        index += len;
        INFO_MSG("init data len[%d]: %d ", i, len);
      }
    }

    return true;
  }

  void OutProgressiveOGG::sendHeader(){
    HTTP_S.Clean(); //make sure no parts of old requests are left in any buffers
    HTTP_S.SetHeader("Content-Type", "video/ogg");
    HTTP_S.protocol = "HTTP/1.0";
    myConn.SendNow(HTTP_S.BuildResponse("200", "OK")); //no SetBody = unknown length - this is intentional, we will stream the entire file
    

    std::map<int, std::deque<std::string> > initData;

    OGG::oggSegment newSegment;
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      if (myMeta.tracks[*it].codec == "theora"){ //get size and position of init data for this page.
        parseInit(myMeta.tracks[*it].init,  initData[*it]);
        pageBuffer[*it].codec = OGG::THEORA;
        pageBuffer[*it].totalFrames = 1; //starts at frame number 1, according to weird offDetectMeta function.
        std::string tempStr = initData[*it][0];
        theora::header tempHead((char *)tempStr.c_str(), 42);
        pageBuffer[*it].split = tempHead.getKFGShift();
        INFO_MSG("got theora KFG shift: %d", pageBuffer[*it].split); //looks OK.
      } else if (myMeta.tracks[*it].codec == "vorbis"){
        parseInit(myMeta.tracks[*it].init,  initData[*it]);
        pageBuffer[*it].codec = OGG::VORBIS;
        pageBuffer[*it].totalFrames = 0;
        pageBuffer[*it].sampleRate = myMeta.tracks[*it].rate;
        pageBuffer[*it].prevBlockFlag = -1;
        vorbis::header tempHead((char *)initData[*it][0].data(), initData[*it][0].size());
        pageBuffer[*it].blockSize[0] = std::min(tempHead.getBlockSize0(), tempHead.getBlockSize1());
        pageBuffer[*it].blockSize[1] = std::max(tempHead.getBlockSize0(), tempHead.getBlockSize1());
        char audioChannels = tempHead.getAudioChannels(); //?
        vorbis::header tempHead2((char *)initData[*it][2].data(), initData[*it][2].size());        
        pageBuffer[*it].vorbisModes = tempHead2.readModeDeque(audioChannels);//getting modes
      } else if (myMeta.tracks[*it].codec == "opus"){
        pageBuffer[*it].totalFrames = 0; //?
        pageBuffer[*it].codec = OGG::OPUS;
        initData[*it].push_back(myMeta.tracks[*it].init);
        initData[*it].push_back(std::string("OpusTags\000\000\000\012MistServer\000\000\000\000", 26));
      }
      pageBuffer[*it].clear(OGG::BeginOfStream, 0, *it, 0);   //CREATES a (map)pageBuffer object, *it = id, pagetype=BOS
      newSegment.dataString = initData[*it].front();
      initData[*it].pop_front();
      pageBuffer[*it].oggSegments.push_back(newSegment);
      pageBuffer[*it].sendTo(myConn, 0); //granule position of 0
    }
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      while (initData[*it].size()){
        newSegment.dataString = initData[*it].front();
        initData[*it].pop_front();
        pageBuffer[*it].oggSegments.push_back(newSegment);
      }
      while (pageBuffer[*it].oggSegments.size()){
        pageBuffer[*it].sendTo(myConn, 0); //granule position of 0
      }
    }
    sentHeader = true;
  }

  void OutProgressiveOGG::onRequest(){
    if (HTTP_R.Read(myConn)){
      DEBUG_MSG(DLVL_DEVEL, "Received request %s", HTTP_R.getUrl().c_str());
      
      if (HTTP_R.method == "OPTIONS" || HTTP_R.method == "HEAD"){
        HTTP_S.Clean();
        HTTP_S.SetHeader("Content-Type", "video/ogg");
        HTTP_S.protocol = "HTTP/1.0";
        HTTP_S.SendResponse("200", "OK", myConn);
        HTTP_S.Clean();
        return;
      }
      
      if (HTTP_R.GetVar("audio") != ""){
        selectedTracks.insert(JSON::Value(HTTP_R.GetVar("audio")).asInt());
      }
      if (HTTP_R.GetVar("video") != ""){
        selectedTracks.insert(JSON::Value(HTTP_R.GetVar("video")).asInt());
      }
      parseData = true;
      wantRequest = false;
      HTTP_R.Clean();
    }
  }
}







