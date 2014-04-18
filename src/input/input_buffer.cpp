#include <iostream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mist/stream.h>
#include <mist/defines.h>

#include "input_buffer.h"

namespace Mist {
  inputBuffer::inputBuffer(Util::Config * cfg) : Input(cfg) {
    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "Buffertime for this stream.";
    option["value"].append(30000LL);
    config->addOption("bufferTime", option);
    
    capa["desc"] = "Enables buffered live input";
    capa["codecs"][0u][0u].append("*");
    capa["codecs"][0u][1u].append("*");
    capa["codecs"][0u][2u].append("*");
    capa["codecs"][0u][3u].append("*");
    capa["codecs"][0u][4u].append("*");
    capa["codecs"][0u][5u].append("*");
    capa["codecs"][0u][6u].append("*");
    capa["codecs"][0u][7u].append("*");
    capa["codecs"][0u][8u].append("*");
    capa["codecs"][0u][9u].append("*");
    DEBUG_MSG(DLVL_DEVEL, "Started MistInBuffer");
    isBuffer = true;
    singleton = this;
    bufferTime = 0;
    cutTime = 0;
    
  }

  void inputBuffer::updateMeta(){
    long long unsigned int firstms = 0xFFFFFFFFFFFFFFFFull;
    long long unsigned int lastms = 0;
    for (std::map<int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (it->second.firstms < firstms){
        firstms = it->second.firstms;
      }
      if (it->second.firstms > lastms){
        lastms = it->second.lastms;
      }
    }
    myMeta.bufferWindow = lastms - firstms;
    myMeta.writeTo(metaPage.mapped);
  } 

  bool inputBuffer::removeKey(unsigned int tid){
    if (myMeta.tracks[tid].keys.size() < 2 || myMeta.tracks[tid].fragments.size() < 2){
      return false;
    }
    DEBUG_MSG(DLVL_HIGH, "Erasing key %d:%d", tid, myMeta.tracks[tid].keys[0].getNumber());
    //remove all parts of this key
    for (int i = 0; i < myMeta.tracks[tid].keys[0].getParts(); i++){
      myMeta.tracks[tid].parts.pop_front();
    }
    //remove the key itself
    myMeta.tracks[tid].keys.pop_front();
    //re-calculate firstms
    myMeta.tracks[tid].firstms = myMeta.tracks[tid].keys[0].getTime();
    //delete the fragment if it's no longer fully buffered
    if (myMeta.tracks[tid].fragments[0].getNumber() < myMeta.tracks[tid].keys[0].getNumber()){
      myMeta.tracks[tid].fragments.pop_front();
      myMeta.tracks[tid].missedFrags ++;
    }
    //if there is more than one page buffered for this track...
    if (inputLoc[tid].size() > 1){
      //Check if the first key starts on the second page or higher
      if (myMeta.tracks[tid].keys[0].getNumber() >= (++(inputLoc[tid].begin()))->first){
        //Find page in indexpage and null it
        for (int i = 0; i < 8192; i += 8){
          int thisKeyNum = ((((long long int *)(indexPages[tid].mapped + i))[0]) >> 32) & 0xFFFFFFFF;
          if (thisKeyNum == htonl(pagesByTrack[tid].begin()->first) && ((((long long int *)(indexPages[tid].mapped + i))[0]) != 0)){
            (((long long int *)(indexPages[tid].mapped + i))[0]) = 0;
          }
        }
        DEBUG_MSG(DLVL_DEVEL, "Erasing track %d, keys %lu-%lu from buffer", tid, inputLoc[tid].begin()->first, inputLoc[tid].begin()->first + inputLoc[tid].begin()->second.keyNum - 1);
        inputLoc[tid].erase(inputLoc[tid].begin());
        dataPages[tid].erase(dataPages[tid].begin());
      }else{
        DEBUG_MSG(DLVL_HIGH, "%d still on first page (%lu - %lu)", myMeta.tracks[tid].keys[0].getNumber(), inputLoc[tid].begin()->first, inputLoc[tid].begin()->first + inputLoc[tid].begin()->second.keyNum - 1);
      }
    }
    return true;
  }

  void inputBuffer::removeUnused(){
    //find the earliest video keyframe stored
    unsigned int firstVideo = 1;
    for(std::map<int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (it->second.type == "video"){
        if (it->second.firstms < firstVideo || firstVideo == 1){
          firstVideo = it->second.firstms;
        }
      }
    }
    for(std::map<int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      //non-video tracks need to have a second keyframe that is <= firstVideo
      if (it->second.type != "video"){
        if (it->second.keys.size() < 2 || it->second.keys[1].getTime() > firstVideo){
          continue;
        }
      }
      //Buffer cutting
      while(it->second.keys.size() > 1 && it->second.keys[0].getTime() < cutTime){
        if (!removeKey(it->first)){break;}
      }
      //Buffer size management
      while(it->second.keys.size() > 1 && (it->second.lastms - it->second.keys[1].getTime()) > bufferTime){
        if (!removeKey(it->first)){break;}
      }
    }
    updateMeta();
  }

  void inputBuffer::userCallback(char * data, size_t len, unsigned int id) {
    /*LTS-START*/
    if (Util::epoch() - lastReTime > 4){
      setup();
    }
    /*LTS-END*/
    unsigned long tmp = ((long)(data[0]) << 24) | ((long)(data[1]) << 16) | ((long)(data[2]) << 8) | ((long)(data[3]));
    if (tmp & 0x80000000) {
      //Track is set to "New track request", assign new track id and create shared memory page
      unsigned long tNum = (givenTracks.size() ? (*givenTracks.rbegin()) : 0) + 1;
      ///\todo Neatify this
      data[0] = (tNum >> 24) & 0xFF;
      data[1] = (tNum >> 16) & 0xFF;
      data[2] = (tNum >> 8) & 0xFF;
      data[3] = (tNum) & 0xFF;
      givenTracks.insert(tNum);
      char tmpChr[100];
      long tmpLen = sprintf(tmpChr, "liveStream_%s%lu", config->getString("streamname").c_str(), tNum);
      metaPages[tNum].init(std::string(tmpChr, tmpLen), 8388608, true);
    } else {
      unsigned long tNum = ((long)(data[0]) << 24) | ((long)(data[1]) << 16) | ((long)(data[2]) << 8) | ((long)(data[3]));
      if (!myMeta.tracks.count(tNum)) {
        DEBUG_MSG(DLVL_DEVEL, "Tracknum not in meta: %lu, from user %u", tNum, id);
        if (metaPages[tNum].mapped) {
          if (metaPages[tNum].mapped[0] == 'D' && metaPages[tNum].mapped[1] == 'T') {
            unsigned int len = ntohl(((int *)metaPages[tNum].mapped)[1]);
            unsigned int i = 0;
            JSON::Value tmpMeta;
            JSON::fromDTMI((const unsigned char *)metaPages[tNum].mapped + 8, len, i, tmpMeta);
            DTSC::Meta tmpTrack(tmpMeta);
            int oldTNum = tmpTrack.tracks.begin()->first;
            bool collision = false;
            for (std::map<int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
              if (it->first == tNum) {
                continue;
              }
              if (it->second.getIdentifier() == tmpTrack.tracks[oldTNum].getIdentifier()) {
                collision = true;
                break;
              }
            }
            if (collision) {
              /// \todo Erasing page for now, should do more here
              DEBUG_MSG(DLVL_DEVEL, "Collision detected! Erasing page for now, should do more here");
              metaPages.erase(tNum);
              data[0] = 0xFF;
              data[1] = 0xFF;
              data[2] = 0xFF;
              data[3] = 0xFF;
            } else {
              if (!myMeta.tracks.count(tNum)) {
                myMeta.tracks[tNum] = tmpTrack.tracks[oldTNum];
                data[4] = 0x00;
                data[5] = 0x00;
                updateMeta();
                char firstPage[100];
                sprintf(firstPage, "%s%lu", config->getString("streamname").c_str(), tNum);
                indexPages[tNum].init(firstPage, 8192, true);
                ((long long int *)indexPages[tNum].mapped)[0] = htonl(1000);
                ///\todo Fix for non-first-key-pushing
                sprintf(firstPage, "%s%lu_0", config->getString("streamname").c_str(), tNum);
                ///\todo Make size dynamic / other solution. 25mb is too much.
                dataPages[tNum][0].init(firstPage, 26214400, true);
              }
            }
          }
        }
      } else {
        //First check if the previous page has been finished:
        if (!inputLoc[tNum].count(dataPages[tNum].rbegin()->first) || !inputLoc[tNum][dataPages[tNum].rbegin()->first].curOffset){
          if (dataPages[tNum].size() > 1){
            int prevPage = (++dataPages[tNum].rbegin())->first;
            //update previous page.
            updateMetaFromPage(tNum, prevPage);
          }
        }
        //update current page
        int curPage = dataPages[tNum].rbegin()->first;
        updateMetaFromPage(tNum, curPage);
        if (inputLoc[tNum][curPage].curOffset > 8388608) {
          //create new page is > 8MB
          int nxtPage = curPage + inputLoc[tNum][curPage].keyNum;
          char nextPageName[100];
          sprintf(nextPageName, "%s%lu_%d", config->getString("streamname").c_str(), tNum, nxtPage);
          dataPages[tNum][nxtPage].init(nextPageName, 20971520, true);
          bool createdNew = false;
          for (int i = 0; i < 8192; i += 8){
            int thisKeyNum = ((((long long int *)(indexPages[tNum].mapped + i))[0]) >> 32) & 0xFFFFFFFF;
            if (thisKeyNum == htonl(curPage)){
              if((ntohl((((long long int*)(indexPages[tNum].mapped + i))[0]) & 0xFFFFFFFF) == 1000)){
                ((long long int *)(indexPages[tNum].mapped + i))[0] &= 0xFFFFFFFF00000000ull;
                ((long long int *)(indexPages[tNum].mapped + i))[0] |= htonl(inputLoc[tNum][curPage].keyNum);
              }
            }
            if (!createdNew && (((long long int*)(indexPages[tNum].mapped + i))[0]) == 0){
              createdNew = true;
              ((long long int *)(indexPages[tNum].mapped + i))[0] = (((long long int)htonl(nxtPage)) << 32) | htonl(1000);
            }
          }
        }
      }
    }
  }

  void inputBuffer::updateMetaFromPage(int tNum, int pageNum){
    DTSC::Packet tmpPack;
    tmpPack.reInit(dataPages[tNum][pageNum].mapped + inputLoc[tNum][pageNum].curOffset, 0);
    while (tmpPack) {
      myMeta.update(tmpPack);
      if (inputLoc[tNum][pageNum].firstTime == 0){
        inputLoc[tNum][pageNum].firstTime = tmpPack.getTime();
      }
      inputLoc[tNum][pageNum].keyNum += tmpPack.getFlag("keyframe");
      inputLoc[tNum][pageNum].curOffset += tmpPack.getDataLen();
      tmpPack.reInit(dataPages[tNum][pageNum].mapped + inputLoc[tNum][pageNum].curOffset, 0);
    }
    updateMeta();
  }

  bool inputBuffer::setup() {
    lastReTime = Util::epoch(); /*LTS*/
    if (!bufferTime){
      bufferTime = config->getInteger("bufferTime");
    }
    JSON::Value servConf = JSON::fromFile(Util::getTmpFolder() + "streamlist");    
    if (servConf.isMember("streams") && servConf["streams"].isMember(config->getString("streamname"))){
      JSON::Value & streamConfig = servConf["streams"][config->getString("streamname")];
      if (streamConfig.isMember("DVR") && streamConfig["DVR"].asInt()){
        if (bufferTime != streamConfig["DVR"].asInt()){
          DEBUG_MSG(DLVL_DEVEL, "Setting bufferTime from %u to new value of %lli", bufferTime, streamConfig["DVR"].asInt());
          bufferTime = streamConfig["DVR"].asInt();
        }
      }
      /*LTS-START*/
      if (streamConfig.isMember("cut") && streamConfig["cut"].asInt()){
        if (cutTime != streamConfig["cut"].asInt()){
          DEBUG_MSG(DLVL_DEVEL, "Setting cutTime from %u to new value of %lli", cutTime, streamConfig["cut"].asInt());
          cutTime = streamConfig["cut"].asInt();
        }
      }
      /*LTS-END*/
    }
    return true;
  }

  bool inputBuffer::readHeader() {
    return true;
  }

  void inputBuffer::getNext(bool smart) {}

  void inputBuffer::seek(int seekTime) {}

  void inputBuffer::trackSelect(std::string trackSpec) {}
}



