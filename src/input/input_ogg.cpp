#include <iostream>
#include <fstream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mist/stream.h>
#include <mist/ogg.h>
#include <mist/defines.h>
#include <mist/bitstream.h>
#include <mist/opus.h>
#include "input_ogg.h"

///\todo Whar be Opus support?

namespace Mist {

  JSON::Value segment::toJSON(OGG::oggCodec myCodec){
    JSON::Value retval;
    retval["time"] = (long long int)time;
    retval["trackid"] = (long long int)tid;
    std::string tmpString = "";
    for (unsigned int i = 0; i < parts.size(); i++){
      tmpString += parts[i];
    }
    retval["data"] = tmpString;
//    INFO_MSG("Setting bpos for packet on track %llu, time %llu, to %llu", tid, time, bytepos);
    retval["bpos"] = (long long int)bytepos;
    if (myCodec == OGG::THEORA){
      if (!theora::isHeader(tmpString.data(), tmpString.size())){
        theora::header tmpHeader((char*)tmpString.data(), tmpString.size());
        if (tmpHeader.getFTYPE() == 0){
          retval["keyframe"] = 1LL;
        }
      }
    }
    return retval;
  }

/*
  unsigned long oggTrack::getBlockSize(unsigned int vModeIndex){ //WTF!!?
    return blockSize[vModes[vModeIndex].blockFlag];

  }
*/
  inputOGG::inputOGG(Util::Config * cfg) : Input(cfg){
    capa["name"] = "OGG";
    capa["desc"] = "Enables OGG input";
    capa["source_match"] = "/*.ogg";
    capa["codecs"][0u][0u].append("theora");
    capa["codecs"][0u][1u].append("vorbis");
    capa["codecs"][0u][1u].append("opus");
  }

  bool inputOGG::checkArguments(){
    if (config->getString("input") == "-"){
      std::cerr << "Input from stream not yet supported" << std::endl;
      return false;
    }
    return true;
  }

  bool inputOGG::preRun(){
    //open File
    inFile = fopen(config->getString("input").c_str(), "r");
    if (!inFile){
      return false;
    }
    return true;
  }

  ///\todo check if all trackID (tid) instances are replaced with bitstream serial numbers
  void inputOGG::parseBeginOfStream(OGG::Page & bosPage){
    //long long int tid = snum2tid.size() + 1;
    unsigned int tid = bosPage.getBitstreamSerialNumber();
    if (memcmp(bosPage.getSegment(0) + 1, "theora", 6) == 0){
      theora::header tmpHead((char*)bosPage.getSegment(0), bosPage.getSegmentLen(0));
      oggTracks[tid].codec = OGG::THEORA;
      oggTracks[tid].msPerFrame = (double)(tmpHead.getFRD() * 1000) / (double)tmpHead.getFRN();   //this should be: 1000/( tmpHead.getFRN()/ tmpHead.getFRD() )
      oggTracks[tid].KFGShift = tmpHead.getKFGShift(); //store KFGShift for granule calculations
      myMeta.tracks[tid].type = "video";
      myMeta.tracks[tid].codec = "theora";
      myMeta.tracks[tid].trackID = tid;
      myMeta.tracks[tid].fpks = (tmpHead.getFRN() * 1000) / tmpHead.getFRD();
      myMeta.tracks[tid].height = tmpHead.getPICH();
      myMeta.tracks[tid].width = tmpHead.getPICW();
      if (!myMeta.tracks[tid].init.size()){
        myMeta.tracks[tid].init = (char)((bosPage.getPayloadSize() >> 8) & 0xFF);
        myMeta.tracks[tid].init += (char)(bosPage.getPayloadSize() & 0xFF);
        myMeta.tracks[tid].init.append(bosPage.getSegment(0), bosPage.getSegmentLen(0));
      }
      INFO_MSG("Track %lu is %s", tid, myMeta.tracks[tid].codec.c_str());
    }
    if (memcmp(bosPage.getSegment(0) + 1, "vorbis", 6) == 0){
      vorbis::header tmpHead((char*)bosPage.getSegment(0), bosPage.getSegmentLen(0));

      oggTracks[tid].codec = OGG::VORBIS;
      oggTracks[tid].msPerFrame = (double)1000.0f / tmpHead.getAudioSampleRate();
      DEBUG_MSG(DLVL_DEVEL, "vorbis trackID: %d msperFrame %f ", tid, oggTracks[tid].msPerFrame);
      oggTracks[tid].channels = tmpHead.getAudioChannels();
      oggTracks[tid].blockSize[0] =  1 << tmpHead.getBlockSize0();
      oggTracks[tid].blockSize[1] =  1 << tmpHead.getBlockSize1();
      //Abusing .contBuffer for temporarily storing the idHeader
      bosPage.getSegment(0, oggTracks[tid].contBuffer);

      myMeta.tracks[tid].type = "audio";
      myMeta.tracks[tid].codec = "vorbis";
      myMeta.tracks[tid].rate = tmpHead.getAudioSampleRate();
      myMeta.tracks[tid].trackID = tid;
      myMeta.tracks[tid].channels = tmpHead.getAudioChannels();
      INFO_MSG("Track %lu is %s", tid, myMeta.tracks[tid].codec.c_str());
    }
    if (memcmp(bosPage.getSegment(0), "OpusHead", 8) == 0){
      oggTracks[tid].codec = OGG::OPUS;
      myMeta.tracks[tid].type = "audio";
      myMeta.tracks[tid].codec = "opus";
      myMeta.tracks[tid].rate = 48000;
      myMeta.tracks[tid].trackID = tid;
      myMeta.tracks[tid].init.assign(bosPage.getSegment(0), bosPage.getSegmentLen(0));
      myMeta.tracks[tid].channels = myMeta.tracks[tid].init[9];
      INFO_MSG("Track %lu is %s", tid, myMeta.tracks[tid].codec.c_str());
    }
  }

  bool inputOGG::readHeader(){
    OGG::Page myPage;
    fseek(inFile, 0, SEEK_SET);
    while (myPage.read(inFile)){ //assumes all headers are sent before any data
      unsigned int tid = myPage.getBitstreamSerialNumber();
      if (myPage.getHeaderType() & OGG::BeginOfStream){
        parseBeginOfStream(myPage);
        INFO_MSG("Read BeginOfStream for track %d", tid);
        continue; //Continue reading next pages
      }

      bool readAllHeaders = true;
      for (std::map<long unsigned int, OGG::oggTrack>::iterator it = oggTracks.begin(); it != oggTracks.end(); it++){
        if (!it->second.parsedHeaders){
          readAllHeaders = false;
          break;
        }
      }
      if (readAllHeaders){
        break;
      }

      // INFO_MSG("tid: %d",tid);

      //Parsing headers
      if (myMeta.tracks[tid].codec == "theora"){
        for (unsigned int i = 0; i < myPage.getAllSegments().size(); i++){
          unsigned long len = myPage.getSegmentLen(i);
          theora::header tmpHead((char*)myPage.getSegment(i),len);
          if (!tmpHead.isHeader()){ //not copying the header anymore, should this check isHeader?
            DEBUG_MSG(DLVL_FAIL, "Theora Header read failed!");
            return false;
          }
          switch (tmpHead.getHeaderType()){
            //Case 0 is being handled by parseBeginOfStream
            case 1: {
                myMeta.tracks[tid].init += (char)((len >> 8) & 0xFF);
                myMeta.tracks[tid].init += (char)(len & 0xFF);
                myMeta.tracks[tid].init.append(myPage.getSegment(i), len);
                break;
              }
            case 2: {
                myMeta.tracks[tid].init += (char)((len >> 8) & 0xFF);
                myMeta.tracks[tid].init += (char)(len & 0xFF);
                myMeta.tracks[tid].init.append(myPage.getSegment(i), len);
                oggTracks[tid].lastGran = 0;
                oggTracks[tid].parsedHeaders = true;
                break;
              }
          }
        }
      }

      if (myMeta.tracks[tid].codec == "vorbis"){
        for (unsigned int i = 0; i < myPage.getAllSegments().size(); i++){
          unsigned long len = myPage.getSegmentLen(i);
          vorbis::header tmpHead((char*)myPage.getSegment(i), len);
          if (!tmpHead.isHeader()){
            DEBUG_MSG(DLVL_FAIL, "Header read failed!");
            return false;
          }
          switch (tmpHead.getHeaderType()){
            //Case 1 is being handled by parseBeginOfStream
            case 3: {
                //we have the first header stored in contBuffer
                myMeta.tracks[tid].init += (char)0x02;
                //ID header size
                for (unsigned int j = 0; j < (oggTracks[tid].contBuffer.size() / 255); j++){
                  myMeta.tracks[tid].init += (char)0xFF;
                }
                myMeta.tracks[tid].init += (char)(oggTracks[tid].contBuffer.size() % 255);
                //Comment header size
                for (unsigned int j = 0; j < (len / 255); j++){
                  myMeta.tracks[tid].init += (char)0xFF;
                }
                myMeta.tracks[tid].init += (char)(len % 255);
                myMeta.tracks[tid].init += oggTracks[tid].contBuffer;
                oggTracks[tid].contBuffer.clear();
                myMeta.tracks[tid].init.append(myPage.getSegment(i), len);
                break;
              }
            case 5: {
                myMeta.tracks[tid].init.append(myPage.getSegment(i), len);
                oggTracks[tid].vModes = tmpHead.readModeDeque(oggTracks[tid].channels);
                oggTracks[tid].parsedHeaders = true;
                break;
              }
          }
        }
      }
      if (myMeta.tracks[tid].codec == "opus"){
        oggTracks[tid].parsedHeaders = true;
      }
    }

    for (std::map<long unsigned int, OGG::oggTrack>::iterator it = oggTracks.begin(); it != oggTracks.end(); it++){
      fseek(inFile, 0, SEEK_SET);
      INFO_MSG("Finding first data for track %lu", it->first);
      position tmp = seekFirstData(it->first);
      if (tmp.trackID){
        currentPositions.insert(tmp);
      } else {
        INFO_MSG("missing track: %lu", it->first);
      }
    }
    getNext();
    while (thisPacket){
      myMeta.update(thisPacket);
      getNext();
    }

    myMeta.toFile(config->getString("input") + ".dtsh");
    return true;
  }

  position inputOGG::seekFirstData(long long unsigned int tid){
    fseek(inFile, 0, SEEK_SET);
    position res;
    res.time = 0;
    res.trackID = tid;
    res.segmentNo = 0;
    bool readSuccesfull = true;
    bool quitloop = false;
    while (!quitloop){
      quitloop = true;
      res.bytepos = ftell(inFile);
      readSuccesfull = oggTracks[tid].myPage.read(inFile);
      if (!readSuccesfull){
        quitloop = true; //break :(
        break;
      }
      if (oggTracks[tid].myPage.getBitstreamSerialNumber() != tid){
        quitloop = false;
        continue;
      }
      if (oggTracks[tid].myPage.getHeaderType() != OGG::Plain){
        quitloop = false;
        continue;
      }
      if (oggTracks[tid].codec == OGG::OPUS){
        if (std::string(oggTracks[tid].myPage.getSegment(0), 2) == "Op"){
          quitloop = false;
        }
      }
      if (oggTracks[tid].codec == OGG::VORBIS){
        vorbis::header tmpHead((char*)oggTracks[tid].myPage.getSegment(0), oggTracks[tid].myPage.getSegmentLen(0));
        if (tmpHead.isHeader()){
          quitloop = false;
        }
      }
      if (oggTracks[tid].codec == OGG::THEORA){
        theora::header tmpHead((char*)oggTracks[tid].myPage.getSegment(0), oggTracks[tid].myPage.getSegmentLen(0));
        if (tmpHead.isHeader()){
          quitloop = false;
        }
      }
    }// while ( oggTracks[tid].myPage.getHeaderType() != OGG::Plain && readSuccesfull && oggTracks[tid].myPage.getBitstreamSerialNumber() != tid);
    INFO_MSG("seek first bytepos: %llu tid: %llu oggTracks[tid].myPage.getHeaderType(): %d ", res.bytepos, tid, oggTracks[tid].myPage.getHeaderType());
    if (!readSuccesfull){
      res.trackID = 0;
    }
    return res;
  }

  void inputOGG::getNext(bool smart){
    if (!currentPositions.size()){
      thisPacket.null();
      return;
    }
    bool lastCompleteSegment = false;
    position curPos = *currentPositions.begin();
    currentPositions.erase(currentPositions.begin());
    segment thisSegment;
    thisSegment.tid = curPos.trackID;
    thisSegment.time = curPos.time;
    thisSegment.bytepos = curPos.bytepos + curPos.segmentNo;
    unsigned int oldSegNo = curPos.segmentNo;
    fseek(inFile, curPos.bytepos, SEEK_SET);
    OGG::Page curPage;
    curPage.read(inFile);
    thisSegment.parts.push_back(std::string(curPage.getSegment(curPos.segmentNo), curPage.getSegmentLen(curPos.segmentNo)));
    bool readFullPacket = false;
    if (curPos.segmentNo == curPage.getAllSegments().size() - 1){
      OGG::Page tmpPage;
      unsigned int bPos;
      while (!readFullPacket){
        bPos = ftell(inFile);//<-- :(
        if (!tmpPage.read(inFile)){
          break;
        }
        if (tmpPage.getBitstreamSerialNumber() != thisSegment.tid){
          continue;
        }
        curPos.bytepos = bPos;
        curPos.segmentNo = 0;
        if (tmpPage.getHeaderType() == OGG::Continued){
          thisSegment.parts.push_back(std::string(tmpPage.getSegment(0), tmpPage.getSegmentLen(0)));
          curPos.segmentNo = 1;
          if (tmpPage.getAllSegments().size() == 1){
            continue;
          }
        } else {
          lastCompleteSegment = true; //if this segment ends on the page, use granule to sync video time
        }
        readFullPacket = true;
      }
    } else {
      curPos.segmentNo++;
     
     // if (oggTracks[thisSegment.tid].codec == OGG::THEORA && curPage.getGranulePosition() != (0xFFFFFFFFFFFFFFFFull) && curPos.segmentNo == curPage.getAllSegments().size() - 1){ //if the next segment is the last one on the page, the (theora) granule should be used to sync the time for the current segment
      if ((oggTracks[thisSegment.tid].codec == OGG::THEORA ||oggTracks[thisSegment.tid].codec == OGG::VORBIS)&& curPage.getGranulePosition() != (0xFFFFFFFFFFFFFFFFull) && curPos.segmentNo == curPage.getAllSegments().size() - 1){ //if the next segment is the last one on the page, the (theora) granule should be used to sync the time for the current segment
        OGG::Page tmpPage;
        while (tmpPage.read(inFile) && tmpPage.getBitstreamSerialNumber() != thisSegment.tid){}
        if ((tmpPage.getBitstreamSerialNumber() == thisSegment.tid) && tmpPage.getHeaderType() == OGG::Continued){
          lastCompleteSegment = true; //this segment should be used to sync time using granule
        }
      }
      readFullPacket = true;
    }
    std::string tmpStr = thisSegment.toJSON(oggTracks[thisSegment.tid].codec).toNetPacked();
    thisPacket.reInit(tmpStr.data(), tmpStr.size());

    if (oggTracks[thisSegment.tid].codec == OGG::VORBIS){
      unsigned long blockSize = 0;
      Utils::bitstreamLSBF packet;
      packet.append((char*)curPage.getSegment(oldSegNo), curPage.getSegmentLen(oldSegNo));
      if (!packet.get(1)){
        //Read index first
        unsigned long vModeIndex = packet.get(vorbis::ilog(oggTracks[thisSegment.tid].vModes.size() - 1));
        blockSize= oggTracks[thisSegment.tid].blockSize[oggTracks[thisSegment.tid].vModes[vModeIndex].blockFlag]; //almost readable.        
      } else {
        DEBUG_MSG(DLVL_WARN, "Packet type != 0");
      }
      curPos.time += oggTracks[thisSegment.tid].msPerFrame * (blockSize / oggTracks[thisSegment.tid].channels);    
    } else if (oggTracks[thisSegment.tid].codec == OGG::THEORA){
      if (lastCompleteSegment == true && curPage.getGranulePosition() != (0xFFFFFFFFFFFFFFFFull)){ //this segment should be used to sync time using granule
        long long unsigned int parseGranuleUpper = curPage.getGranulePosition() >> oggTracks[thisSegment.tid].KFGShift ;
        long long unsigned int parseGranuleLower(curPage.getGranulePosition() & ((1 << oggTracks[thisSegment.tid].KFGShift) - 1));
        thisSegment.time = oggTracks[thisSegment.tid].msPerFrame * (parseGranuleUpper + parseGranuleLower - 1);
        curPos.time = thisSegment.time;
        std::string tmpStr = thisSegment.toJSON(oggTracks[thisSegment.tid].codec).toNetPacked();
        thisPacket.reInit(tmpStr.data(), tmpStr.size());
        //  INFO_MSG("thisTime: %d", thisPacket.getTime());
      }
      curPos.time += oggTracks[thisSegment.tid].msPerFrame;
    } else if (oggTracks[thisSegment.tid].codec == OGG::OPUS){
      if (thisSegment.parts.size()){
        curPos.time += Opus::Opus_getDuration(thisSegment.parts.front().data());
      }
    }
    if (readFullPacket){
      currentPositions.insert(curPos);
    }
  }//getnext()

  long long unsigned int inputOGG::calcGranuleTime(unsigned long tid, long long unsigned int granule){
    switch (oggTracks[tid].codec){
      case OGG::VORBIS:
        return granule * oggTracks[tid].msPerFrame ; //= samples * samples per second
        break;
      case OGG::OPUS:
        return granule / 48; //always 48kHz
        break;
      case OGG::THEORA:{
        long long unsigned int parseGranuleUpper = granule >> oggTracks[tid].KFGShift ;
        long long unsigned int parseGranuleLower = (granule & ((1 << oggTracks[tid].KFGShift) - 1));
        return (parseGranuleUpper + parseGranuleLower) * oggTracks[tid].msPerFrame ; //= frames * msPerFrame
        break;
      }
      default:
        DEBUG_MSG(DLVL_WARN, "Unknown codec, can not calculate time from granule");
        break;
    }
    return 0;
  }

  #ifndef _GNU_SOURCE  
    void * memrchr(const void *s, int c, size_t n){      
      const unsigned char *cp;
      if (n != 0) {
        cp = (unsigned char *)s + n;
        do {
          if (*(--cp) == (unsigned char)c)
            return((void *)cp);
        } while (--n != 0);
      }
      return((void *)0);
    }  
  #endif 

  void inputOGG::seek(int seekTime){
    currentPositions.clear();
    DEBUG_MSG(DLVL_MEDIUM, "Seeking to %dms", seekTime);

    //for every track
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      //find first keyframe before keyframe with ms > seektime
      position tmpPos;
      tmpPos.trackID = *it;
      tmpPos.time = myMeta.tracks[*it].keys.begin()->getTime();
      tmpPos.bytepos = myMeta.tracks[*it].keys.begin()->getBpos();
      for (std::deque<DTSC::Key>::iterator ot = myMeta.tracks[*it].keys.begin(); ot != myMeta.tracks[*it].keys.end(); ot++){
        if (ot->getTime() > seekTime){
          break;
        } else {
          tmpPos.time = ot->getTime();
          tmpPos.bytepos = ot->getBpos();
        }
      }
      INFO_MSG("Found %dms for track %lu at %llu bytepos %llu", seekTime, *it, tmpPos.time, tmpPos.bytepos);
      int backChrs=std::min(280ull, tmpPos.bytepos - 1);
      fseek(inFile, tmpPos.bytepos - backChrs, SEEK_SET);
      char buffer[300];
      fread(buffer, 300, 1, inFile);
      char * loc = buffer + backChrs + 2; //start at tmpPos.bytepos+2
      while (loc && !(loc[0] == 'O' && loc[1] == 'g' && loc[2] == 'g' && loc[3] == 'S')){
        loc = (char *)memrchr(buffer, 'O',  (loc-buffer) -1 );//seek reverse
      }
      if (!loc){
        INFO_MSG("Unable to find a page boundary starting @ %llu, track %lu", tmpPos.bytepos, *it);        
        continue;
      }
      tmpPos.segmentNo = backChrs - (loc - buffer);
      tmpPos.bytepos -= tmpPos.segmentNo;
      INFO_MSG("Track %lu, segment %llu found at bytepos %llu", *it, tmpPos.segmentNo, tmpPos.bytepos);

      currentPositions.insert(tmpPos);
    }
  }

  void inputOGG::trackSelect(std::string trackSpec){
    selectedTracks.clear();
    size_t index;
    while (trackSpec != ""){
      index = trackSpec.find(' ');
      selectedTracks.insert(atoll(trackSpec.substr(0, index).c_str()));
      if (index != std::string::npos){
        trackSpec.erase(0, index + 1);
      } else {
        trackSpec = "";
      }
    }
  }
}








