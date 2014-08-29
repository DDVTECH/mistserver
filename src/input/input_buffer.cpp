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
    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "Buffertime for this stream.";
    option["value"].append(30000LL);
    config->addOption("bufferTime", option);
    /*LTS-start*/
    option.null();
    option["arg"] = "string";
    option["long"] = "record";
    option["short"] = "r";
    option["help"] = "Record the stream to a file";
    option["value"].append("");
    config->addOption("record", option);
    /*LTS-end*/
        
    
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
    IPC::semaphore liveMeta(std::string("liveMeta@" + config->getString("streamname")).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
    liveMeta.wait();
    memset(metaPage.mapped+myMeta.getSendLen(), 0, metaPage.len > myMeta.getSendLen() ? std::min(metaPage.len-myMeta.getSendLen(), 4ll) : 0);
    liveMeta.post();
  } 

  bool inputBuffer::removeKey(unsigned int tid){
    if (myMeta.tracks[tid].keys.size() < 2 || myMeta.tracks[tid].fragments.size() < 2){
      return false;
    }
    DEBUG_MSG(DLVL_HIGH, "Erasing key %d:%d", tid, myMeta.tracks[tid].keys[0].getNumber());
    //remove all parts of this key
    for (int i = 0; i < myMeta.tracks[tid].keys[0].getParts(); i++){
      /*LTS-START*/
      if (recFile.is_open()){
        if (!recMeta.tracks.count(tid)){
          recMeta.tracks[tid] = myMeta.tracks[tid];
          recMeta.tracks[tid].reset();
        }
      }
      /*LTS-END*/
      myMeta.tracks[tid].parts.pop_front();
    }
    /*LTS-START*/
    ///\todo Maybe optimize this by keeping track of the byte positions
    if (recFile.good()){
      long long unsigned int firstms = myMeta.tracks[tid].keys[0].getTime();
      long long unsigned int lastms = myMeta.tracks[tid].keys[1].getTime();
      DEBUG_MSG(DLVL_DEVEL, "Recording track %d from %llums to %llums", tid, firstms, lastms);
      long long unsigned int bpos = 0;
      DTSC::Packet recPack;
      int pageLen = dataPages[tid][inputLoc[tid].begin()->first].len;
      char * pageMapped = dataPages[tid][inputLoc[tid].begin()->first].mapped;
      while( bpos < (unsigned long long)pageLen) {
        int tmpSize = ((int)pageMapped[bpos + 4] << 24) | ((int)pageMapped[bpos + 5] << 16) | ((int)pageMapped[bpos + 6] << 8) | (int)pageMapped[bpos + 7];
        tmpSize += 8;
        recPack.reInit(pageMapped + bpos, tmpSize, true);
        if (tmpSize != recPack.getDataLen()){
          DEBUG_MSG(DLVL_DEVEL, "Something went wrong while trying to record a packet @ %llu, %d != %d", bpos, tmpSize, recPack.getDataLen());
          break;
        }
        if (recPack.getTime() >= lastms){
          DEBUG_MSG(DLVL_HIGH, "Stopping record, %llu >= %llu", recPack.getTime(), lastms);
          break;
        }
        if (recPack.getTime() >= firstms){
          //Actually record to file here
          JSON::Value recJSON = recPack.toJSON();
          recJSON["bpos"] = recBpos;
          recFile << recJSON.toNetPacked();
          recFile.flush();
          recBpos += recPack.getDataLen();
          recMeta.update(recJSON);
        }
        bpos += recPack.getDataLen();
      }
      recFile.flush();
      std::ofstream tmp(std::string(recName + ".dtsh").c_str());
      tmp << recMeta.toJSON().toNetPacked();
      tmp.close();
    }
    /*LTS-END*/
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
    /*LTS-START*/
    if (Util::epoch() - lastReTime > 4){
      setup();
    }
    /*LTS-END*/
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
        metaPages[tmpTid].init(tempMetaName, 8388608, true);
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
          /*LTS
          int finalMap = -1;
          LTS*/
          /*LTS-START*/
          int collidesWith = -1;
          for (std::map<int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
            if (it->second.getIdentifier() == tempId) {
              collidesWith = it->first;
              break;
            }
          }
          /*LTS-END*/
          /*LTS
          if (tmpMeta.tracks.begin()->second.type == "video"){
            finalMap = 1;
          }
          if (tmpMeta.tracks.begin()->second.type == "audio"){
            finalMap = 2;
          }
          LTS*/
          //Remove the "negotiate" status in either case
          negotiateTracks.erase(value);
          metaPages.erase(value);
          /*LTS
          if (finalMap != -1 && givenTracks.count(finalMap)) {
          LTS*/
          if (collidesWith != -1 && givenTracks.count(collidesWith)) {/*LTS*/
            /*LTS
            DEBUG_MSG(DLVL_DEVEL, "Collision of new track %lu with track %d detected! Declining track", value, finalMap);
            LTS*/
            WARN_MSG("Collision of new track %lu with track %d detected! Declining track", value, collidesWith);
            thisData[0] = 0xFF;
            thisData[1] = 0xFF;
            thisData[2] = 0xFF;
            thisData[3] = 0xFF;
          } else {
            int finalMap = collidesWith;/*LTS*/
            if (finalMap == -1){
              /*LTS-START*/
              finalMap = (myMeta.tracks.size() ? myMeta.tracks.rbegin()->first : 0) + 1;
              DEBUG_MSG(DLVL_DEVEL, "No colision detected for negotiation track %lu, from user %u, assigning final track number %d", value, id, finalMap);
              /*LTS-END*/
              /*LTS
              WARN_MSG("Invalid track type detected, declining.");
              thisData[0] = 0xFF;
              thisData[1] = 0xFF;
              thisData[2] = 0xFF;
              thisData[3] = 0xFF;
              continue;
              LTS*/
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
            dataPages[finalMap][0].init(firstPage, 26214400, true);
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
        if (inputLoc[value][currentPage].curOffset > 8388608) {
          int nextPage = currentPage + inputLoc[value][currentPage].keyNum;
          char nextPageName[100];
          sprintf(nextPageName, "%s%lu_%d", config->getString("streamname").c_str(), value, nextPage);
          dataPages[value][nextPage].init(nextPageName, 20971520, true);
          DEVEL_MSG("Created page %s", nextPageName);
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

      if (streamConfig.isMember("record") && streamConfig["record"].asString() != ""){
        if (recName != streamConfig["record"].asString()){
          //close currently recording file, for we should open a new one
          recFile.close();
          recMeta.tracks.clear();
        }
        if (!recFile.is_open()){
          recName = streamConfig["record"].asString();
          DEBUG_MSG(DLVL_DEVEL, "Starting to record stream %s to %s", config->getString("streamname").c_str(), recName.c_str());
          recFile.open(recName.c_str());
          if (recFile.fail()){
            DEBUG_MSG(DLVL_DEVEL, "Error occured during record opening: %s", strerror(errno));
          }
          recBpos = 0;
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



