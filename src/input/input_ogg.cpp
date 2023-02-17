#include "input_ogg.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mist/bitstream.h>
#include <mist/defines.h>
#include <mist/ogg.h>
#include <mist/opus.h>
#include <mist/stream.h>
#include <string>

///\todo Whar be Opus support?

namespace Mist{

  JSON::Value segment::toJSON(OGG::oggCodec myCodec){
    JSON::Value retval;
    retval["time"] = time;
    retval["trackid"] = tid;
    std::string tmpString = "";
    for (size_t i = 0; i < parts.size(); i++){tmpString += parts[i];}
    retval["data"] = tmpString;
    retval["bpos"] = bytepos;
    if (myCodec == OGG::THEORA){
      if (!theora::isHeader(tmpString.data(), tmpString.size())){
        theora::header tmpHeader((char *)tmpString.data(), tmpString.size());
        if (tmpHeader.getFTYPE() == 0){retval["keyframe"] = 1;}
      }
    }
    return retval;
  }

  inputOGG::inputOGG(Util::Config *cfg) : Input(cfg){
    capa["name"] = "OGG";
    capa["desc"] = "This input allows streaming of OGG files as Video on Demand.";
    capa["source_match"] = "/*.ogg";
    capa["source_file"] = "$source";
    capa["codecs"]["video"].append("theora");
    capa["codecs"]["audio"].append("vorbis");
    capa["codecs"]["audio"].append("opus");
  }

  bool inputOGG::checkArguments(){
    if (config->getString("input") == "-"){
      std::cerr << "Input from stream not yet supported" << std::endl;
      return false;
    }
    return true;
  }

  bool inputOGG::preRun(){
    // open File
    inFile = fopen(config->getString("input").c_str(), "r");
    if (!inFile){return false;}
    return true;
  }

  ///\todo check if all trackID (tid) instances are replaced with bitstream serial numbers
  void inputOGG::parseBeginOfStream(OGG::Page &bosPage){
    // long long int tid = snum2tid.size() + 1;
    size_t tid = bosPage.getBitstreamSerialNumber();
    size_t idx = M.trackIDToIndex(tid, getpid());
    if (idx == INVALID_TRACK_ID){idx = meta.addTrack();}
    if (memcmp(bosPage.getSegment(0) + 1, "theora", 6) == 0){
      theora::header tmpHead((char *)bosPage.getSegment(0), bosPage.getSegmentLen(0));
      oggTracks[idx].codec = OGG::THEORA;
      oggTracks[idx].msPerFrame = (double)(tmpHead.getFRD() * 1000) / (double)tmpHead.getFRN(); // this should be: 1000/( tmpHead.getFRN()/ tmpHead.getFRD() )
      oggTracks[idx].KFGShift = tmpHead.getKFGShift(); // store KFGShift for granule calculations
      meta.setType(idx, "video");
      meta.setCodec(idx, "theora");
      meta.setID(idx, tid);
      meta.setFpks(idx, (double)(tmpHead.getFRN() * 1000) / tmpHead.getFRD());
      meta.setHeight(idx, tmpHead.getPICH());
      meta.setWidth(idx, tmpHead.getPICW());
      if (!M.getInit(idx).size()){
        std::string init = "  ";
        Bit::htobs((char *)init.data(), bosPage.getPayloadSize());
        init.append(bosPage.getSegment(0), bosPage.getSegmentLen(0));
        meta.setInit(idx, init);
      }
      INFO_MSG("Track with id %zu is %s", tid, M.getCodec(tid).c_str());
    }
    if (memcmp(bosPage.getSegment(0) + 1, "vorbis", 6) == 0){
      vorbis::header tmpHead((char *)bosPage.getSegment(0), bosPage.getSegmentLen(0));

      oggTracks[idx].codec = OGG::VORBIS;
      oggTracks[idx].msPerFrame = (double)1000.0f / tmpHead.getAudioSampleRate();
      oggTracks[idx].channels = tmpHead.getAudioChannels();
      oggTracks[idx].blockSize[0] = 1 << tmpHead.getBlockSize0();
      oggTracks[idx].blockSize[1] = 1 << tmpHead.getBlockSize1();
      DEVEL_MSG("vorbis trackID: %zu msperFrame %f ", tid, oggTracks[idx].msPerFrame);
      // Abusing .contBuffer for temporarily storing the idHeader
      bosPage.getSegment(0, oggTracks[idx].contBuffer);

      meta.setType(idx, "audio");
      meta.setCodec(idx, "vorbis");
      meta.setRate(idx, tmpHead.getAudioSampleRate());
      meta.setID(idx, tid);
      meta.setChannels(idx, tmpHead.getAudioChannels());
      INFO_MSG("Track with id %zu is %s", tid, M.getCodec(idx).c_str());
    }
    if (memcmp(bosPage.getSegment(0), "OpusHead", 8) == 0){
      oggTracks[idx].codec = OGG::OPUS;
      meta.setType(idx, "audio");
      meta.setCodec(idx, "opus");
      meta.setRate(idx, 48000);
      meta.setID(idx, tid);
      meta.setInit(idx, bosPage.getSegment(0), bosPage.getSegmentLen(0));
      meta.setChannels(idx, M.getInit(idx)[9]);
      INFO_MSG("Track with id %zu is %s", tid, M.getCodec(idx).c_str());
    }
  }

  bool inputOGG::readHeader(){
    meta.reInit(config->getString("streamname"), true);
    OGG::Page myPage;
    fseek(inFile, 0, SEEK_SET);
    while (myPage.read(inFile)){// assumes all headers are sent before any data
      size_t tid = myPage.getBitstreamSerialNumber();
      size_t idx = M.trackIDToIndex(tid, getpid());
      if (myPage.getHeaderType() & OGG::BeginOfStream){
        parseBeginOfStream(myPage);
        INFO_MSG("Read BeginOfStream for track %zu", tid);
        continue; // Continue reading next pages
      }

      bool readAllHeaders = true;
      for (std::map<size_t, OGG::oggTrack>::iterator it = oggTracks.begin(); it != oggTracks.end(); it++){
        if (!it->second.parsedHeaders){
          readAllHeaders = false;
          break;
        }
      }
      if (readAllHeaders){break;}

      // Parsing headers
      if (M.getCodec(idx) == "theora"){
        for (size_t i = 0; i < myPage.getAllSegments().size(); i++){
          size_t len = myPage.getSegmentLen(i);
          theora::header tmpHead((char *)myPage.getSegment(i), len);
          if (!tmpHead.isHeader()){// not copying the header anymore, should this check isHeader?
            FAIL_MSG("Theora Header read failed!");
            return false;
          }
          switch (tmpHead.getHeaderType()){
          // Case 0 is being handled by parseBeginOfStream
          case 1:{
            std::string init = M.getInit(idx);
            init += (char)((len >> 8) & 0xFF);
            init += (char)(len & 0xFF);
            init.append(myPage.getSegment(i), len);
            meta.setInit(idx, init);
            break;
          }
          case 2:{
            std::string init = M.getInit(idx);
            init += (char)((len >> 8) & 0xFF);
            init += (char)(len & 0xFF);
            init.append(myPage.getSegment(i), len);
            meta.setInit(idx, init);
            oggTracks[idx].lastGran = 0;
            oggTracks[idx].parsedHeaders = true;
            break;
          }
          }
        }
      }

      if (M.getCodec(idx) == "vorbis"){
        for (size_t i = 0; i < myPage.getAllSegments().size(); i++){
          size_t len = myPage.getSegmentLen(i);
          vorbis::header tmpHead((char *)myPage.getSegment(i), len);
          if (!tmpHead.isHeader()){
            FAIL_MSG("Header read failed!");
            return false;
          }
          switch (tmpHead.getHeaderType()){
          // Case 1 is being handled by parseBeginOfStream
          case 3:{
            // we have the first header stored in contBuffer
            std::string init = M.getInit(idx);
            init += (char)0x02;
            // ID header size
            for (size_t j = 0; j < (oggTracks[idx].contBuffer.size() / 255); j++){
              init += (char)0xFF;
            }
            init += (char)(oggTracks[idx].contBuffer.size() % 255);
            // Comment header size
            for (size_t j = 0; j < (len / 255); j++){init += (char)0xFF;}
            init += (char)(len % 255);
            init += oggTracks[idx].contBuffer;
            oggTracks[idx].contBuffer.clear();
            init.append(myPage.getSegment(i), len);
            meta.setInit(idx, init);
            break;
          }
          case 5:{
            std::string init = M.getInit(idx);
            init.append(myPage.getSegment(i), len);
            meta.setInit(idx, init);
            oggTracks[idx].vModes = tmpHead.readModeDeque(oggTracks[idx].channels);
            oggTracks[idx].parsedHeaders = true;
            break;
          }
          }
        }
      }
      if (M.getCodec(idx) == "opus"){oggTracks[idx].parsedHeaders = true;}
    }

    for (std::map<size_t, OGG::oggTrack>::iterator it = oggTracks.begin(); it != oggTracks.end(); it++){
      fseek(inFile, 0, SEEK_SET);
      INFO_MSG("Finding first data for track %zu", it->first);
      position tmp = seekFirstData(it->first);
      currentPositions.insert(tmp);
    }
    getNext();
    while (thisPacket){
      meta.update(thisPacket);
      getNext();
    }
    return true;
  }

  position inputOGG::seekFirstData(size_t idx){
    fseek(inFile, 0, SEEK_SET);
    position res;
    res.time = 0;
    res.trackID = idx;
    res.segmentNo = 0;
    bool readSuccesfull = true;
    bool quitloop = false;
    while (!quitloop){
      quitloop = true;
      res.bytepos = ftell(inFile);
      readSuccesfull = oggTracks[idx].myPage.read(inFile);
      if (!readSuccesfull){
        quitloop = true; // break :(
        break;
      }
      if (oggTracks[idx].myPage.getBitstreamSerialNumber() != M.getID(idx)){
        quitloop = false;
        continue;
      }
      if (oggTracks[idx].myPage.getHeaderType() != OGG::Plain){
        quitloop = false;
        continue;
      }
      if (oggTracks[idx].codec == OGG::OPUS){
        if (std::string(oggTracks[idx].myPage.getSegment(0), 2) == "Op"){quitloop = false;}
      }
      if (oggTracks[idx].codec == OGG::VORBIS){
        vorbis::header tmpHead((char *)oggTracks[idx].myPage.getSegment(0),
                               oggTracks[idx].myPage.getSegmentLen(0));
        if (tmpHead.isHeader()){quitloop = false;}
      }
      if (oggTracks[idx].codec == OGG::THEORA){
        theora::header tmpHead((char *)oggTracks[idx].myPage.getSegment(0),
                               oggTracks[idx].myPage.getSegmentLen(0));
        if (tmpHead.isHeader()){quitloop = false;}
      }
    }
    INFO_MSG("seek first bytepos: %" PRIu64 " tid: %zu oggTracks[idx].myPage.getHeaderType(): %d ",
             res.bytepos, idx, oggTracks[idx].myPage.getHeaderType());
    if (!readSuccesfull){res.trackID = 0;}
    return res;
  }

  void inputOGG::getNext(size_t idx){
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
    size_t oldSegNo = curPos.segmentNo;
    fseek(inFile, curPos.bytepos, SEEK_SET);
    OGG::Page curPage;
    curPage.read(inFile);
    thisSegment.parts.push_back(
        std::string(curPage.getSegment(curPos.segmentNo), curPage.getSegmentLen(curPos.segmentNo)));
    bool readFullPacket = false;
    if (curPos.segmentNo == curPage.getAllSegments().size() - 1){
      OGG::Page tmpPage;
      uint64_t bPos;
      while (!readFullPacket){
        bPos = ftell(inFile); //<-- :(
        if (!tmpPage.read(inFile)){break;}
        if (tmpPage.getBitstreamSerialNumber() != thisSegment.tid){continue;}
        curPos.bytepos = bPos;
        curPos.segmentNo = 0;
        if (tmpPage.getHeaderType() == OGG::Continued){
          thisSegment.parts.push_back(std::string(tmpPage.getSegment(0), tmpPage.getSegmentLen(0)));
          curPos.segmentNo = 1;
          if (tmpPage.getAllSegments().size() == 1){continue;}
        }else{
          lastCompleteSegment = true; // if this segment ends on the page, use granule to sync video time
        }
        readFullPacket = true;
      }
    }else{
      curPos.segmentNo++;

      if ((oggTracks[curPos.trackID].codec == OGG::THEORA || oggTracks[curPos.trackID].codec == OGG::VORBIS) &&
          curPage.getGranulePosition() != (0xFFFFFFFFFFFFFFFFull) &&
          curPos.segmentNo == curPage.getAllSegments().size() -
                                  1){// if the next segment is the last one on the page, the (theora) granule
                                       // should be used to sync the time for the current segment
        OGG::Page tmpPage;
        while (tmpPage.read(inFile) && tmpPage.getBitstreamSerialNumber() != thisSegment.tid){}
        if ((tmpPage.getBitstreamSerialNumber() == thisSegment.tid) && tmpPage.getHeaderType() == OGG::Continued){
          lastCompleteSegment = true; // this segment should be used to sync time using granule
        }
      }
      readFullPacket = true;
    }
    std::string tmpStr = thisSegment.toJSON(oggTracks[curPos.trackID].codec).toNetPacked();
    thisPacket.reInit(tmpStr.data(), tmpStr.size());

    if (oggTracks[curPos.trackID].codec == OGG::VORBIS){
      size_t blockSize = 0;
      Utils::bitstreamLSBF packet;
      packet.append((char *)curPage.getSegment(oldSegNo), curPage.getSegmentLen(oldSegNo));
      if (!packet.get(1)){
        // Read index first
        size_t vModeIndex = packet.get(vorbis::ilog(oggTracks[curPos.trackID].vModes.size() - 1));
        blockSize =
            oggTracks[curPos.trackID].blockSize[oggTracks[curPos.trackID].vModes[vModeIndex].blockFlag]; // almost
                                                                                                         // readable.
      }else{
        WARN_MSG("Packet type != 0");
      }
      curPos.time += oggTracks[curPos.trackID].msPerFrame * (blockSize / oggTracks[curPos.trackID].channels);
    }else if (oggTracks[curPos.trackID].codec == OGG::THEORA){
      if (lastCompleteSegment == true && curPage.getGranulePosition() != (0xFFFFFFFFFFFFFFFFull)){// this segment should be used to sync time using granule
        uint64_t parseGranuleUpper = curPage.getGranulePosition() >> oggTracks[curPos.trackID].KFGShift;
        uint64_t parseGranuleLower(curPage.getGranulePosition() &
                                   ((1 << oggTracks[curPos.trackID].KFGShift) - 1));
        thisSegment.time = oggTracks[curPos.trackID].msPerFrame * (parseGranuleUpper + parseGranuleLower - 1);
        curPos.time = thisSegment.time;
        std::string tmpStr = thisSegment.toJSON(oggTracks[curPos.trackID].codec).toNetPacked();
        thisPacket.reInit(tmpStr.data(), tmpStr.size());
        //  INFO_MSG("thisTime: %d", thisPacket.getTime());
      }
      curPos.time += oggTracks[curPos.trackID].msPerFrame;
    }else if (oggTracks[curPos.trackID].codec == OGG::OPUS){
      if (thisSegment.parts.size()){
        curPos.time += Opus::Opus_getDuration(thisSegment.parts.front().data());
      }
    }
    if (readFullPacket){currentPositions.insert(curPos);}
  }// getnext()

  uint64_t inputOGG::calcGranuleTime(size_t tid, uint64_t granule){
    size_t idx = M.trackIDToIndex(tid, getpid());
    switch (oggTracks[idx].codec){
    case OGG::VORBIS:
      return granule * oggTracks[idx].msPerFrame; //= samples * samples per second
      break;
    case OGG::OPUS:
      return granule / 48; // always 48kHz
      break;
    case OGG::THEORA:{
      uint64_t parseGranuleUpper = granule >> oggTracks[idx].KFGShift;
      uint64_t parseGranuleLower = (granule & ((1 << oggTracks[idx].KFGShift) - 1));
      return (parseGranuleUpper + parseGranuleLower) * oggTracks[idx].msPerFrame; //= frames * msPerFrame
      break;
    }
    default: WARN_MSG("Unknown codec, can not calculate time from granule"); break;
    }
    return 0;
  }

#ifndef _GNU_SOURCE
  void *memrchr(const void *s, int c, size_t n){
    const unsigned char *cp;
    if (n != 0){
      cp = (unsigned char *)s + n;
      do{
        if (*(--cp) == (unsigned char)c) return ((void *)cp);
      }while (--n != 0);
    }
    return ((void *)0);
  }
#endif

  void inputOGG::seek(uint64_t seekTime, size_t idx){
    currentPositions.clear();
    MEDIUM_MSG("Seeking to %" PRIu64 "ms", seekTime);

    // for every track
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      // find first keyframe before keyframe with ms > seektime
      position tmpPos;
      tmpPos.trackID = it->first;
      DTSC::Keys keys(M.keys(it->first));
      tmpPos.time = keys.getTime(keys.getFirstValid());
      tmpPos.bytepos = keys.getBpos(keys.getFirstValid());
      for (size_t i = keys.getFirstValid(); i < keys.getEndValid(); ++i){
        if (keys.getTime(i) > seekTime){
          break;
        }else{
          tmpPos.time = keys.getTime(i);
          tmpPos.bytepos = keys.getBpos(i);
        }
      }
      INFO_MSG("Found %" PRIu64 "ms for track %zu at %" PRIu64 " bytepos %" PRIu64, seekTime,
               it->first, tmpPos.time, tmpPos.bytepos);
      int backChrs = std::min((uint64_t)280, tmpPos.bytepos - 1);
      fseek(inFile, tmpPos.bytepos - backChrs, SEEK_SET);
      char buffer[300];
      fread(buffer, 300, 1, inFile);
      char *loc = buffer + backChrs + 2; // start at tmpPos.bytepos+2
      while (loc && !(loc[0] == 'O' && loc[1] == 'g' && loc[2] == 'g' && loc[3] == 'S')){
        loc = (char *)memrchr(buffer, 'O', (loc - buffer) - 1); // seek reverse
      }
      if (!loc){
        INFO_MSG("Unable to find a page boundary starting @ %" PRIu64 ", track %zu", tmpPos.bytepos, it->first);
        continue;
      }
      tmpPos.segmentNo = backChrs - (loc - buffer);
      tmpPos.bytepos -= tmpPos.segmentNo;
      INFO_MSG("Track %zu, segment %" PRIu64 " found at bytepos %" PRIu64, it->first, tmpPos.segmentNo,
               tmpPos.bytepos);

      currentPositions.insert(tmpPos);
    }
  }
}// namespace Mist
