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
    myMeta.vod = false;
    myMeta.live = true;
    myMeta.writeTo(metaPage.mapped);
    memset(metaPage.mapped+myMeta.getSendLen(), 0, metaPage.len > myMeta.getSendLen() ? std::min(metaPage.len-myMeta.getSendLen(), 4ll) : 0);
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
          unsigned int thisKeyNum = ((((long long int *)(indexPages[tid].mapped + i))[0]) >> 32) & 0xFFFFFFFF;
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
    static int nextTempId = 1001;
    char counter = (*(data - 1));
    for (int index = 0; index < 5; index++){
      char* thisData = data + (index * 6);
      unsigned long value = ((long)(thisData[0]) << 24) | ((long)(thisData[1]) << 16) | ((long)(thisData[2]) << 8) | thisData[3];
      if (value == 0xFFFFFFFF){
        //Skip value 0xFFFFFFFF as this indicates a previously declined track
        continue;
      }
      if (counter == 126 || counter == 127 || counter == 254 || counter == 255){
        if (negotiateTracks.count(value)){
          negotiateTracks.erase(value);
          metaPages.erase(value);
        }
        if (givenTracks.count(value)){
          givenTracks.erase(value);
        }
        continue;
      }
      if (value & 0x80000000){
        //Track is set to "New track request", assign new track id and create shared memory page
        int tmpTid = nextTempId++;
        negotiateTracks.insert(tmpTid);
        thisData[0] = (tmpTid >> 24) & 0xFF;
        thisData[1] = (tmpTid >> 16) & 0xFF;
        thisData[2] = (tmpTid >> 8) & 0xFF;
        thisData[3] = (tmpTid) & 0xFF;
        unsigned long tNum = ((long)(thisData[4]) << 8) | thisData[5];
        INFO_MSG("Assigning temporary ID %d to incoming track %lu for user %d", tmpTid, tNum, id);
        
        char tempMetaName[100];
        sprintf(tempMetaName, "liveStream_%s%d", config->getString("streamname").c_str(), tmpTid);
        metaPages[tmpTid].init(tempMetaName, 8388608, true);
      }
      if (negotiateTracks.count(value)){
        INFO_MSG("Negotiating %lu", value);
        //Track is currently under negotiation, check whether the metadata has been submitted
        if (metaPages[value].mapped){
          INFO_MSG("Mapped %lu", value);
          unsigned int len = ntohl(((int *)metaPages[value].mapped)[1]);
          unsigned int i = 0;
          JSON::Value JSONMeta;
          JSON::fromDTMI((const unsigned char *)metaPages[value].mapped + 8, len, i, JSONMeta);
          DTSC::Meta tmpMeta(JSONMeta);
          if (!tmpMeta.tracks.count(value)){//Track not yet added
            continue;
          }

          std::string tempId = tmpMeta.tracks.begin()->second.getIdentifier();
          DEBUG_MSG(DLVL_DEVEL, "Attempting colision detection for track %s", tempId.c_str());
          int finalMap = -1;
          for (std::map<int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
            if (it->second.type == "video"){
              finalMap = 1;
            }
            if (it->second.type == "audio"){
              finalMap = 2;
            }
          }
          //Remove the "negotiate" status in either case
          negotiateTracks.erase(value);
          metaPages.erase(value);
          if (finalMap != -1 && givenTracks.count(finalMap)) {
            DEBUG_MSG(DLVL_DEVEL, "Collision of new track %lu with track %d detected! Declining track", value, finalMap);
            thisData[0] = 0xFF;
            thisData[1] = 0xFF;
            thisData[2] = 0xFF;
            thisData[3] = 0xFF;
          } else {
            if (finalMap == -1){
              DEBUG_MSG(DLVL_DEVEL, "Invalid track type detected, discarding");
              continue;
            }else{
              //Resume either if we have more than 1 keyframe on the replacement track (assume it was already pushing before the track "dissapeared"
              //or if the firstms of the replacement track is later than the lastms on the existing track
              if (tmpMeta.tracks.begin()->second.keys.size() > 1|| tmpMeta.tracks.begin()->second.firstms >= myMeta.tracks[finalMap].lastms){
                DEBUG_MSG(DLVL_DEVEL, "Allowing negotiation track %lu, from user %u, to resume pushing final track number %d", value, id, finalMap);
              }else{
              //Otherwise replace existing track
                DEBUG_MSG(DLVL_DEVEL, "Re-push initiated for track %lu, from user %u, will replace final track number %d", value, id, finalMap);
                myMeta.tracks.erase(finalMap);
              }
            }
            givenTracks.insert(finalMap);
            if (!myMeta.tracks.count(finalMap)){
              myMeta.tracks[finalMap] = tmpMeta.tracks.begin()->second;
              myMeta.tracks[finalMap].trackID = finalMap;
            }
            thisData[0] = (finalMap >> 24) & 0xFF;
            thisData[1] = (finalMap >> 16) & 0xFF;
            thisData[2] = (finalMap >> 8) & 0xFF;
            thisData[3] = (finalMap) & 0xFF;
            int keyNum = myMeta.tracks[finalMap].keys.size();
            thisData[4] = (keyNum >> 8) & 0xFF;
            thisData[5] = keyNum & 0xFF;
            updateMeta();
            char firstPage[100];
            sprintf(firstPage, "%s%d", config->getString("streamname").c_str(), finalMap);
            indexPages[finalMap].init(firstPage, 8192, true);
            ((long long int *)indexPages[finalMap].mapped)[0] = htonl(1000);
            sprintf(firstPage, "%s%d_%d", config->getString("streamname").c_str(), finalMap, keyNum);
            ///\todo Make size dynamic / other solution. 25mb is too much.
            dataPages[finalMap][0].init(firstPage, 26214400, true);
          }
        }
      }
      if (givenTracks.count(value)){
        //First check if the previous page has been finished:
        if (!inputLoc[value].count(dataPages[value].rbegin()->first) || !inputLoc[value][dataPages[value].rbegin()->first].curOffset){
          if (dataPages[value].size() > 1){
            int previousPage = (++dataPages[value].rbegin())->first;
            updateMetaFromPage(value, previousPage);
          }
        }
        //update current page
        int currentPage = dataPages[value].rbegin()->first;
        updateMetaFromPage(value, currentPage);
        if (inputLoc[value][currentPage].curOffset > 8388608) {
          int nextPage = currentPage + inputLoc[value][currentPage].keyNum;
          char nextPageName[100];
          sprintf(nextPageName, "%s%lu_%d", config->getString("streamname").c_str(), value, nextPage);
          dataPages[value][nextPage].init(nextPageName, 20971520, true);
          bool createdNew = false;
          for (int i = 0; i < 8192; i += 8){
            unsigned int thisKeyNum = ((((long long int *)(indexPages[value].mapped + i))[0]) >> 32) & 0xFFFFFFFF;
            if (thisKeyNum == htonl(currentPage)){
              if((ntohl((((long long int*)(indexPages[value].mapped + i))[0]) & 0xFFFFFFFF) == 1000)){
                ((long long int *)(indexPages[value].mapped + i))[0] &= 0xFFFFFFFF00000000ull;
                ((long long int *)(indexPages[value].mapped + i))[0] |= htonl(inputLoc[value][currentPage].keyNum);
              }
            }
            if (!createdNew && (((long long int*)(indexPages[value].mapped + i))[0]) == 0){
              createdNew = true;
              ((long long int *)(indexPages[value].mapped + i))[0] = (((long long int)htonl(nextPage)) << 32) | htonl(1000);
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



