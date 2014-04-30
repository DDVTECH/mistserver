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

#include "input_ogg.h"

namespace Mist {
  inputOGG::inputOGG(Util::Config * cfg) : Input(cfg) {
    capa["decs"] = "Enables OGG Input";
    capa["codecs"][0u][0u].append("theora");
    capa["codecs"][0u][1u].append("vorbis");
  }

  bool inputOGG::setup() {
    if (config->getString("input") == "-") {
      std::cerr << "Input from stdin not yet supported" << std::endl;
      return false;
    }
    if (!config->getBool("player")){
      if (config->getString("output") == "-") {
        std::cerr << "Output to stdout not yet supported" << std::endl;
        return false;
      }
    }else{
      if (config->getString("output") != "-") {
        std::cerr << "File output in player mode not supported" << std::endl;
        return false;
      }
    }
    
    //open File
    inFile = fopen(config->getString("input").c_str(), "r");
    if (!inFile) {
      return false;
    }
    return true;
  }

  void inputOGG::parseBeginOfStream(OGG::Page & bosPage) {
    long long int tid = snum2tid.size() + 1;
    snum2tid[bosPage.getBitstreamSerialNumber()] = tid;
    if (!memcmp(bosPage.getFullPayload() + 1, "theora", 6)) {
      oggTracks[tid].codec = THEORA;
      theora::header tmpHead(bosPage.getFullPayload(), bosPage.getPayloadSize());
      oggTracks[tid].msPerFrame = (double)(tmpHead.getFRD() * 1000) / tmpHead.getFRN();
    }
    if (!memcmp(bosPage.getFullPayload() + 1, "vorbis", 6)) {
      oggTracks[tid].codec = VORBIS;
      vorbis::header tmpHead(bosPage.getFullPayload(), bosPage.getPayloadSize());
      oggTracks[tid].msPerFrame = (double)1000 / ntohl(tmpHead.getAudioSampleRate());
    }
  }

  bool inputOGG::readHeader() {
    JSON::Value lastPack;
    if (!inFile) {
      return false;
    }
    //See whether a separate header file exists.
    DTSC::File tmp(config->getString("input") + ".dtsh");
    if (tmp) {
      myMeta = tmp.getMeta();
      return true;
    }
    //Create header file from OGG data
    fseek(inFile, 0, SEEK_SET);
    OGG::Page tmpPage;
    long long int lastBytePos = 0;
    while (tmpPage.read(inFile)) {
      DEBUG_MSG(DLVL_WARN,"Read a page");
      if (tmpPage.getHeaderType() & OGG::BeginOfStream){
        parseBeginOfStream(tmpPage);
        DEBUG_MSG(DLVL_WARN,"Read BOS page for stream %lu, now track %lld", tmpPage.getBitstreamSerialNumber(), snum2tid[tmpPage.getBitstreamSerialNumber()]);
      }
      int offset = 0;
      long long int tid = snum2tid[tmpPage.getBitstreamSerialNumber()];
      for (std::deque<unsigned int>::iterator it = tmpPage.getSegmentTableDeque().begin(); it != tmpPage.getSegmentTableDeque().end(); it++) {
        if (oggTracks[tid].parsedHeaders) {
          DEBUG_MSG(DLVL_WARN,"Parsing a page segment on track %lld", tid);
          if ((it == (tmpPage.getSegmentTableDeque().end() - 1)) && (int)(tmpPage.getPageSegments()) == 255 && (int)(tmpPage.getSegmentTable()[254]) == 255) {
            oggTracks[tid].contBuffer.append(tmpPage.getFullPayload() + offset, (*it));
          } else {
            lastPack["trackid"] = tid;
            lastPack["time"] = (long long)oggTracks[tid].lastTime;
            if (oggTracks[tid].contBuffer.size()) {
              lastPack["data"] = oggTracks[tid].contBuffer + std::string(tmpPage.getFullPayload() + offset, (*it));
              oggTracks[tid].contBuffer.clear();
            } else {
              lastPack["data"] = std::string(tmpPage.getFullPayload() + offset, (*it));
            }
            if (oggTracks[tid].codec == VORBIS) {
              unsigned int blockSize = 0;
              Utils::bitstreamLSBF packet;
              packet.append(lastPack["data"].asString());
              if (!packet.get(1)) {
                blockSize = oggTracks[tid].blockSize[oggTracks[tid].vModes[packet.get(vorbis::ilog(oggTracks[tid].vModes.size() - 1))].blockFlag];
              } else {
                DEBUG_MSG(DLVL_WARN, "Packet type != 0");
              }
              oggTracks[tid].lastTime += oggTracks[tid].msPerFrame * (blockSize / oggTracks[tid].channels);
            }
            if (oggTracks[tid].codec == THEORA) {
              oggTracks[tid].lastTime += oggTracks[tid].msPerFrame;
              if (it == (tmpPage.getSegmentTableDeque().end() - 1)) {
                if (oggTracks[tid].idHeader.parseGranuleUpper(oggTracks[tid].lastGran) != oggTracks[tid].idHeader.parseGranuleUpper(tmpPage.getGranulePosition())) {
                  lastPack["keyframe"] = 1ll;
                  oggTracks[tid].lastGran = tmpPage.getGranulePosition();
                } else {
                  lastPack["interframe"] = 1ll;
                }
              }
            }
            lastPack["bpos"] = 0ll;
            DEBUG_MSG(DLVL_WARN,"Parsed a packet of track %lld, new timestamp %f", tid, oggTracks[tid].lastTime);
            myMeta.update(lastPack);
          }
        } else {
          //Parsing headers
          switch (oggTracks[tid].codec) {
            case THEORA: {
                theora::header tmpHead(tmpPage.getFullPayload() + offset, (*it));
                DEBUG_MSG(DLVL_WARN,"Theora header, type %d", tmpHead.getHeaderType());
                switch (tmpHead.getHeaderType()) {
                  case 0: {
                      oggTracks[tid].idHeader = tmpHead;
                      myMeta.tracks[tid].height = tmpHead.getPICH();
                      myMeta.tracks[tid].width = tmpHead.getPICW();
                      myMeta.tracks[tid].idHeader = std::string(tmpPage.getFullPayload() + offset, (*it));
                      break;
                    }
                  case 1: {
                      myMeta.tracks[tid].commentHeader = std::string(tmpPage.getFullPayload() + offset, (*it));
                      break;
                    }
                  case 2: {
                      myMeta.tracks[tid].codec = "theora";
                      myMeta.tracks[tid].trackID = tid;
                      myMeta.tracks[tid].type = "video";
                      myMeta.tracks[tid].init = std::string(tmpPage.getFullPayload() + offset, (*it));
                      oggTracks[tid].parsedHeaders = true;
                      oggTracks[tid].lastGran = 0;
                      break;
                    }
                }
                break;
              }
            case VORBIS: {
                vorbis::header tmpHead(tmpPage.getFullPayload() + offset, (*it));
                DEBUG_MSG(DLVL_WARN,"Vorbis header, type %d", tmpHead.getHeaderType());
                switch (tmpHead.getHeaderType()) {
                  case 1: {
                      myMeta.tracks[tid].channels = tmpHead.getAudioChannels();
                      myMeta.tracks[tid].idHeader = std::string(tmpPage.getFullPayload() + offset, (*it));
                      oggTracks[tid].channels = tmpHead.getAudioChannels();
                      oggTracks[tid].blockSize[0] =  1 << tmpHead.getBlockSize0();
                      oggTracks[tid].blockSize[1] =  1 << tmpHead.getBlockSize1();
                      break;
                    }
                  case 3: {
                      myMeta.tracks[tid].commentHeader = std::string(tmpPage.getFullPayload() + offset, (*it));
                      break;
                    }
                  case 5: {
                      myMeta.tracks[tid].codec = "vorbis";
                      myMeta.tracks[tid].trackID = tid;
                      myMeta.tracks[tid].type = "audio";
                      DEBUG_MSG(DLVL_WARN,"Set default values");
                      myMeta.tracks[tid].init = std::string(tmpPage.getFullPayload() + offset, (*it));
                      DEBUG_MSG(DLVL_WARN,"Set init values");
                      oggTracks[tid].vModes = tmpHead.readModeDeque(oggTracks[tid].channels);
                      DEBUG_MSG(DLVL_WARN,"Set vmodevalues");
                      oggTracks[tid].parsedHeaders = true;
                      break;
                    }
                }
                break;
              }
          }
          offset += (*it);
        }
      }
      lastBytePos = ftell(inFile);
      DEBUG_MSG(DLVL_WARN,"End of Loop, @ filepos %lld", lastBytePos);
    }
    DEBUG_MSG(DLVL_WARN,"Exited while loop");
    std::ofstream oFile(std::string(config->getString("input") + ".dtsh").c_str());
    oFile << myMeta.toJSON().toNetPacked();
    oFile.close();
    return true;
  }

  bool inputOGG::seekNextPage(int tid){
    fseek(inFile, oggTracks[tid].lastPageOffset, SEEK_SET);
    bool res = true;
    do {
      res = oggTracks[tid].myPage.read(inFile);
    } while(res && snum2tid[oggTracks[tid].myPage.getBitstreamSerialNumber()] != tid);
    oggTracks[tid].lastPageOffset = ftell(inFile);
    oggTracks[tid].nxtSegment = 0;
    return res;
  }

  void inputOGG::getNext(bool smart) {
    if (!sortedSegments.size()){
      for (std::set<int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
        seekNextPage((*it));
      }
    }
    if (sortedSegments.size()){
      int tid = (*(sortedSegments.begin())).tid;
      bool addedPacket = false;
      while (!addedPacket){
        segPart tmpPart;
        if (oggTracks[tid].myPage.getSegment(oggTracks[tid].nxtSegment, tmpPart.segData, tmpPart.len)){
          if (oggTracks[tid].nxtSegment == 0 && oggTracks[tid].myPage.getHeaderType() && OGG::Continued){
            segment tmpSeg = *(sortedSegments.begin());
            tmpSeg.parts.push_back(tmpPart);
            sortedSegments.erase(sortedSegments.begin());
            sortedSegments.insert(tmpSeg);
          }else{
            segment tmpSeg;
            tmpSeg.parts.push_back(tmpPart);
            tmpSeg.tid = tid;
            tmpSeg.time = oggTracks[tid].lastTime;
            if (oggTracks[tid].codec == VORBIS) {
              std::string data;
              data.append(tmpPart.segData, tmpPart.len);
              unsigned int blockSize = 0;
              Utils::bitstreamLSBF packet;
              packet.append(data);
              if (!packet.get(1)) {
                blockSize = oggTracks[tid].blockSize[oggTracks[tid].vModes[packet.get(vorbis::ilog(oggTracks[tid].vModes.size() - 1))].blockFlag];
              }
              oggTracks[tid].lastTime += oggTracks[tid].msPerFrame * (blockSize / oggTracks[tid].channels);
            }
            if (oggTracks[tid].codec == THEORA) {
              oggTracks[tid].lastTime += oggTracks[tid].msPerFrame;
            }
            sortedSegments.insert(tmpSeg);
            addedPacket = true;
          }
          oggTracks[tid].nxtSegment ++;
        }else{
          if (!seekNextPage(tid)){
            break;
          }
        }
      }  
      std::string data;
    }
  }

  void inputOGG::seek(int seekTime) {
    DEBUG_MSG(DLVL_WARN,"Seeking is not yet supported for ogg files");
    //Do nothing, seeking is not yet implemented for ogg
  }

  void inputOGG::trackSelect(std::string trackSpec) {
    selectedTracks.clear();
    long long int index;
    while (trackSpec != "") {
      index = trackSpec.find(' ');
      selectedTracks.insert(atoi(trackSpec.substr(0, index).c_str()));
      DEBUG_MSG(DLVL_WARN, "Added track %d, index = %lld, (index == npos) = %d", atoi(trackSpec.substr(0, index).c_str()), index, index == std::string::npos);
      if (index != std::string::npos) {
        trackSpec.erase(0, index + 1);
      } else {
        trackSpec = "";
      }
    }
  }
}


