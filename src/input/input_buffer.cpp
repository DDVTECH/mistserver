#include <fcntl.h>
#include <sys/stat.h>

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
    capa["name"] = "Buffer";
    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "DVR buffer time in ms";
    option["value"].append(50000LL);
    config->addOption("bufferTime", option);
    capa["optional"]["DVR"]["name"] = "Buffer time (ms)";
    capa["optional"]["DVR"]["help"] = "The target available buffer time for this live stream, in milliseconds. This is the time available to seek around in, and will automatically be extended to fit whole keyframes.";
    capa["optional"]["DVR"]["option"] = "--buffer";
    capa["optional"]["DVR"]["type"] = "uint";
    capa["optional"]["DVR"]["default"] = 50000LL;
    capa["source_match"] = "push://*";
    capa["priority"] = 9ll;
    capa["desc"] = "Provides buffered live input";
    capa["codecs"][0u][0u].append("*");
    capa["codecs"][0u][1u].append("*");
    capa["codecs"][0u][2u].append("*");
    isBuffer = true;
    singleton = this;
    bufferTime = 0;
    cutTime = 0;
    
  }

  inputBuffer::~inputBuffer(){
    if (myMeta.tracks.size()){
      DEBUG_MSG(DLVL_DEVEL, "Cleaning up, removing last keyframes");
      for(std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        while (removeKey(it->first)){}
      }
    }
  }

  void inputBuffer::updateMeta(){
    long long unsigned int firstms = 0xFFFFFFFFFFFFFFFFull;
    long long unsigned int lastms = 0;
    for (std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
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
    IPC::semaphore liveMeta(std::string("liveMeta@" + config->getString("streamname")).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
    liveMeta.wait();
    myMeta.writeTo(metaPage.mapped);
    memset(metaPage.mapped+myMeta.getSendLen(), 0, metaPage.len > myMeta.getSendLen() ? std::min(metaPage.len-myMeta.getSendLen(), 4ll) : 0);
    liveMeta.post();
  } 

  bool inputBuffer::removeKey(unsigned int tid){
    if ((myMeta.tracks[tid].keys.size() < 2 || myMeta.tracks[tid].fragments.size() < 2) && config->is_active){
      return false;
    }
    if (!myMeta.tracks[tid].keys.size()){
      return false;
    }
    DEBUG_MSG(DLVL_HIGH, "Erasing key %d:%d", tid, myMeta.tracks[tid].keys[0].getNumber());
    //remove all parts of this key
    for (int i = 0; i < myMeta.tracks[tid].keys[0].getParts(); i++){
      myMeta.tracks[tid].parts.pop_front();
    }
    //remove the key itself
    myMeta.tracks[tid].keys.pop_front();
    myMeta.tracks[tid].keySizes.pop_front();
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
          unsigned int thisKeyNum = ntohl(((((long long int *)(indexPages[tid].mapped + i))[0]) >> 32) & 0xFFFFFFFF);
          if (thisKeyNum < myMeta.tracks[tid].keys[0].getNumber()){
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
    for(std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (it->second.type == "video"){
        if (it->second.firstms < firstVideo || firstVideo == 1){
          firstVideo = it->second.firstms;
        }
      }
    }
    for(std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
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
      if (value == 0){
        //Skip value 0 as this indicates an empty track
        continue;
      }
      if (pushedLoc[value] == thisData){
        if (counter == 126 || counter == 127 || counter == 254 || counter == 255){
          pushedLoc.erase(value);
          if (negotiateTracks.count(value)){
            negotiateTracks.erase(value);
            metaPages.erase(value);
          }
          if (data[4] == 0xFF && data[5] == 0xFF && givenTracks.count(value)){
            givenTracks.erase(value);
            inputLoc.erase(value);
          }
          continue;
        }
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
        DEBUG_MSG(DLVL_HIGH, "Assigning temporary ID %d to incoming track %lu for user %d", tmpTid, tNum, id);
        
        char tempMetaName[100];
        sprintf(tempMetaName, "liveStream_%s%d", config->getString("streamname").c_str(), tmpTid);
        metaPages[tmpTid].init(tempMetaName, DEFAULT_META_PAGE_SIZE, true);
      }
      if (negotiateTracks.count(value)){
        //Track is currently under negotiation, check whether the metadata has been submitted
        if (metaPages[value].mapped){
          unsigned int len = ntohl(((int *)metaPages[value].mapped)[1]);
          unsigned int i = 0;
          JSON::Value JSONMeta;
          JSON::fromDTMI((const unsigned char *)metaPages[value].mapped + 8, len, i, JSONMeta);
          DTSC::Meta tmpMeta(JSONMeta);
          if (!tmpMeta.tracks.count(value)){//Track not yet added
            continue;
          }

          std::string tempId = tmpMeta.tracks.begin()->second.getIdentifier();
          DEBUG_MSG(DLVL_HIGH, "Attempting colision detection for track %s", tempId.c_str());
          int finalMap = -1;
          if (tmpMeta.tracks.begin()->second.type == "video"){
            finalMap = 1;
          }
          if (tmpMeta.tracks.begin()->second.type == "audio"){
            finalMap = 2;
          }
          //Remove the "negotiate" status in either case
          negotiateTracks.erase(value);
          metaPages.erase(value);
          if (finalMap != -1 && givenTracks.count(finalMap)) {
            WARN_MSG("Collision of new track %lu with track %d detected! Declining track", value, finalMap);
            thisData[0] = 0xFF;
            thisData[1] = 0xFF;
            thisData[2] = 0xFF;
            thisData[3] = 0xFF;
          } else {
            if (finalMap == -1){
              WARN_MSG("Invalid track type detected, declining.");
              thisData[0] = 0xFF;
              thisData[1] = 0xFF;
              thisData[2] = 0xFF;
              thisData[3] = 0xFF;
              continue;
            }else{
              //Resume either if we have more than 1 keyframe on the replacement track (assume it was already pushing before the track "dissapeared"
              //or if the firstms of the replacement track is later than the lastms on the existing track
              if (tmpMeta.tracks.begin()->second.keys.size() > 1 || !myMeta.tracks.count(finalMap) || tmpMeta.tracks.begin()->second.firstms >= myMeta.tracks[finalMap].lastms){
                if (myMeta.tracks.count(finalMap) && myMeta.tracks[finalMap].lastms > 0){
                  INFO_MSG("Allowing negotiation track %lu, from user %u, to resume pushing final track number %d", value, id, finalMap);
                }else{
                  INFO_MSG("Allowing negotiation track %lu, from user %u, to start pushing final track number %d", value, id, finalMap);
                }
              }else{
              //Otherwise replace existing track
                INFO_MSG("Re-push initiated for track %lu, from user %u, will replace final track number %d", value, id, finalMap);
                myMeta.tracks.erase(finalMap);
                dataPages.erase(finalMap);
                inputLoc.erase(finalMap);
              }
            }
            givenTracks.insert(finalMap);
            pushedLoc[finalMap] = thisData;
            if (!myMeta.tracks.count(finalMap)){
              DEBUG_MSG(DLVL_HIGH, "Inserting metadata for track number %d", finalMap);
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
            dataPages[finalMap][0].init(firstPage, DEFAULT_DATA_PAGE_SIZE, true);
          }
        }
      }
      if (givenTracks.count(value) && pushedLoc[value] == thisData){
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
        if (inputLoc[value][currentPage].curOffset > FLIP_DATA_PAGE_SIZE) {
          int nextPage = currentPage + inputLoc[value][currentPage].keyNum;
          char nextPageName[100];
          sprintf(nextPageName, "%s%lu_%d", config->getString("streamname").c_str(), value, nextPage);
          dataPages[value][nextPage].init(nextPageName, DEFAULT_DATA_PAGE_SIZE, true);
          DEVEL_MSG("Created page %s, from pos %llu", nextPageName, inputLoc[value][currentPage].curOffset);
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
          if (!createdNew){
            ERROR_MSG("Could not create index for new page - out of empty indexes!");
          }
        }
      }
    }
  }

  void inputBuffer::updateMetaFromPage(int tNum, int pageNum){
    DTSC::Packet tmpPack;
    tmpPack.reInit(dataPages[tNum][pageNum].mapped + inputLoc[tNum][pageNum].curOffset, 0);
    if (!tmpPack && inputLoc[tNum][pageNum].curOffset == 0){
      return;
    }
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
    std::string strName = config->getString("streamname");
    Util::sanitizeName(strName);
    strName = strName.substr(0,(strName.find_first_of("+ ")));
    IPC::sharedPage serverCfg("!mistConfig", DEFAULT_CONF_PAGE_SIZE, false, false); ///< Contains server configuration and capabilities
    IPC::semaphore configLock("!mistConfLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
    configLock.wait();
    DTSC::Scan streamCfg = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("streams").getMember(strName);
    long long tmpNum;
    
    //if stream is configured and setting is present, use it, always
    if (streamCfg && streamCfg.getMember("DVR")){
      tmpNum = streamCfg.getMember("DVR").asInt();
    }else{
      if (streamCfg){
        //otherwise, if stream is configured use the default
        tmpNum = config->getOption("bufferTime", true)[0u].asInt();
      }else{
        //if not, use the commandline argument
        tmpNum = config->getOption("bufferTime").asInt();
      }
    }
    //if the new value is different, print a message and apply it
    if (bufferTime != tmpNum){
      DEBUG_MSG(DLVL_DEVEL, "Setting bufferTime from %u to new value of %lli", bufferTime, tmpNum);
      bufferTime = tmpNum;
    }
    
    configLock.post();
    configLock.close();
    return true;
  }

  bool inputBuffer::readHeader() {
    return true;
  }

  void inputBuffer::getNext(bool smart) {}

  void inputBuffer::seek(int seekTime) {}

  void inputBuffer::trackSelect(std::string trackSpec) {}
}



