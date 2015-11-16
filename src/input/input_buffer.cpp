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
#include <mist/triggers.h>

#include "input_buffer.h"

#ifndef TIMEOUTMULTIPLIER
#define TIMEOUTMULTIPLIER 2
#endif

namespace Mist {
  inputBuffer::inputBuffer(Util::Config * cfg) : Input(cfg){
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
    /*LTS-start*/
    option.null();
    option["arg"] = "string";
    option["long"] = "record";
    option["short"] = "r";
    option["help"] = "Record the stream to a file";
    option["value"].append("");
    config->addOption("record", option);
    capa["optional"]["record"]["name"] = "Record to file";
    capa["optional"]["record"]["help"] = "Filename to record the stream to.";
    capa["optional"]["record"]["option"] = "--record";
    capa["optional"]["record"]["type"] = "str";
    capa["optional"]["record"]["default"] = "";
    option.null();

    option["arg"] = "integer";
    option["long"] = "cut";
    option["short"] = "c";
    option["help"] = "Any timestamps before this will be cut from the live buffer";
    option["value"].append(0LL);
    config->addOption("cut", option);
    capa["optional"]["cut"]["name"] = "Cut time (ms)";
    capa["optional"]["cut"]["help"] = "Any timestamps before this will be cut from the live buffer.";
    capa["optional"]["cut"]["option"] = "--cut";
    capa["optional"]["cut"]["type"] = "uint";
    capa["optional"]["cut"]["default"] = 0LL;
    option.null();

    option["arg"] = "integer";
    option["long"] = "segment-size";
    option["short"] = "S";
    option["help"] = "Target time duration in milliseconds for segments";
    option["value"].append(5000LL);
    config->addOption("segmentsize", option);
    capa["optional"]["segmentsize"]["name"] = "Segment size (ms)";
    capa["optional"]["segmentsize"]["help"] = "Target time duration in milliseconds for segments.";
    capa["optional"]["segmentsize"]["option"] = "--segment-size";
    capa["optional"]["segmentsize"]["type"] = "uint";
    capa["optional"]["segmentsize"]["default"] = 5000LL;
    option.null();
    option["arg"] = "integer";
    option["long"] = "udp-port";
    option["short"] = "U";
    option["help"] = "The UDP port on which to listen for TS Packets";
    option["value"].append(0LL);
    config->addOption("udpport", option);
    capa["optional"]["udpport"]["name"] = "TS/UDP port";
    capa["optional"]["udpport"]["help"] = "The UDP port on which to listen for TS Packets, or 0 for disabling TS Input";
    capa["optional"]["udpport"]["option"] = "--udp-port";
    capa["optional"]["udpport"]["type"] = "uint";
    capa["optional"]["udpport"]["default"] = 0LL;
    /*LTS-end*/
    capa["source_match"] = "push://*";
    capa["priority"] = 9ll;
    capa["desc"] = "Provides buffered live input";
    capa["codecs"][0u][0u].append("*");
    capa["codecs"][0u][1u].append("*");
    capa["codecs"][0u][2u].append("*");
    isBuffer = true;
    singleton = this;
    bufferTime = 50000;
    cutTime = 0;
    segmentSize = 5000;
  }

  inputBuffer::~inputBuffer(){
    config->is_active = false;
    if (myMeta.tracks.size()){
      DEBUG_MSG(DLVL_DEVEL, "Cleaning up, removing last keyframes");
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        std::map<unsigned long, DTSCPageData> & locations = bufferLocations[it->first];
        if (!metaPages.count(it->first) || !metaPages[it->first].mapped){
          continue;
        }
        //First detect all entries on metaPage
        for (int i = 0; i < 8192; i += 8){
          int * tmpOffset = (int *)(metaPages[it->first].mapped + i);
          if (tmpOffset[0] == 0 && tmpOffset[1] == 0){
            continue;
          }
          unsigned long keyNum = ntohl(tmpOffset[0]);

          //Add an entry into bufferLocations[tNum] for the pages we haven't handled yet.
          if (!locations.count(keyNum)){
            locations[keyNum].curOffset = 0;
          }
          locations[keyNum].pageNum = keyNum;
          locations[keyNum].keyNum = ntohl(tmpOffset[1]);
        }
        for (std::map<unsigned long, DTSCPageData>::iterator it2 = locations.begin(); it2 != locations.end(); it2++){
          char thisPageName[NAME_BUFFER_SIZE];
          snprintf(thisPageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, config->getString("streamname").c_str(), it->first, it2->first);
          IPC::sharedPage erasePage(thisPageName, 20971520);
          erasePage.master = true;
        }
      }
    }
  }

  void inputBuffer::updateMeta(){
    long long unsigned int firstms = 0xFFFFFFFFFFFFFFFFull;
    long long unsigned int lastms = 0;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
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
    static char liveSemName[NAME_BUFFER_SIZE];
    snprintf(liveSemName, NAME_BUFFER_SIZE, SEM_LIVE, streamName.c_str());
    IPC::semaphore liveMeta(liveSemName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
    liveMeta.wait();
    if (!metaPages.count(0) || !metaPages[0].mapped){
      char pageName[NAME_BUFFER_SIZE];
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_INDEX, streamName.c_str());
      metaPages[0].init(pageName, DEFAULT_META_PAGE_SIZE,  true);
      metaPages[0].master = false;
    }
    myMeta.writeTo(metaPages[0].mapped);
    memset(metaPages[0].mapped + myMeta.getSendLen(), 0, (metaPages[0].len > myMeta.getSendLen() ? std::min(metaPages[0].len - myMeta.getSendLen(), 4ll) : 0));
    liveMeta.post();
  }

  bool inputBuffer::removeKey(unsigned int tid){
    if ((myMeta.tracks[tid].keys.size() < 2 || myMeta.tracks[tid].fragments.size() < 2) && config->is_active){
      return false;
    }
    if (!myMeta.tracks[tid].keys.size()){
      return false;
    }
    DEBUG_MSG(DLVL_HIGH, "Erasing key %d:%lu", tid, myMeta.tracks[tid].keys[0].getNumber());
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
    ///\todo Fix recording
    /*LTS-START
    ///\todo Maybe optimize this by keeping track of the byte positions
    if (recFile.good()){
      long long unsigned int firstms = myMeta.tracks[tid].keys[0].getTime();
      long long unsigned int lastms = myMeta.tracks[tid].lastms;
      if (myMeta.tracks[tid].keys.size() > 1){
        lastms = myMeta.tracks[tid].keys[1].getTime();
      }
      DEBUG_MSG(DLVL_DEVEL, "Recording track %d from %llums to %llums", tid, firstms, lastms);
      long long unsigned int bpos = 0;
      DTSC::Packet recPack;
      int pageLen = dataPages[tid][bufferLocations[tid].begin()->first].len;
      char * pageMapped = dataPages[tid][bufferLocations[tid].begin()->first].mapped;
      while( bpos < (unsigned long long)pageLen){
        int tmpSize = ((int)pageMapped[bpos + 4] << 24) | ((int)pageMapped[bpos + 5] << 16) | ((int)pageMapped[bpos + 6] << 8) | (int)pageMapped[bpos + 7];
        tmpSize += 8;
        recPack.reInit(pageMapped + bpos, tmpSize, true);
        if (tmpSize != recPack.getDataLen()){
          DEBUG_MSG(DLVL_DEVEL, "Something went wrong while trying to record a packet @ %llu, %d != %d", bpos, tmpSize, recPack.getDataLen());
          break;
        }
        if (recPack.getTime() >= lastms){/// \todo getTime never reaches >= lastms, so probably the recording bug has something to do with this
          DEBUG_MSG(DLVL_HIGH, "Stopping record, %llu >= %llu", recPack.getTime(), lastms);
          break;
        }
        if (recPack.getTime() >= firstms){
          //Actually record to file here
          JSON::Value recJSON = recPack.toJSON();
          recJSON["bpos"] = recBpos;
          recFile << recJSON.toNetPacked();
          recFile.flush();
          recBpos = recFile.tellp();
          recMeta.update(recJSON);
        }
        bpos += recPack.getDataLen();
      }
      recFile.flush();
      std::ofstream tmp(std::string(recName + ".dtsh").c_str());
      tmp << recMeta.toJSON().toNetPacked();
      tmp.close();
    }
    LTS-END*/
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
    if (bufferLocations[tid].size() > 1){
      //Check if the first key starts on the second page or higher
      if (myMeta.tracks[tid].keys[0].getNumber() >= (++(bufferLocations[tid].begin()))->first || !config->is_active){
        //Find page in indexpage and null it
        for (int i = 0; i < 8192; i += 8){
          unsigned int thisKeyNum = ((((long long int *)(metaPages[tid].mapped + i))[0]) >> 32) & 0xFFFFFFFF;
          if (thisKeyNum == htonl(bufferLocations[tid].begin()->first) && ((((long long int *)(metaPages[tid].mapped + i))[0]) != 0)){
            (((long long int *)(metaPages[tid].mapped + i))[0]) = 0;
          }
        }
        DEBUG_MSG(DLVL_DEVEL, "Erasing track %d, keys %lu-%lu from buffer", tid, bufferLocations[tid].begin()->first, bufferLocations[tid].begin()->first + bufferLocations[tid].begin()->second.keyNum - 1);
        bufferRemove(tid, bufferLocations[tid].begin()->first);
        for (int i = 0; i < 1024; i++){
          int * tmpOffset = (int *)(metaPages[tid].mapped + (i * 8));
          int tmpNum = ntohl(tmpOffset[0]);
          if (tmpNum == bufferLocations[tid].begin()->first){
            tmpOffset[0] = 0;
            tmpOffset[1] = 0;
          }
        }

        curPageNum.erase(tid);
        char thisPageName[NAME_BUFFER_SIZE];
        snprintf(thisPageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, config->getString("streamname").c_str(), (unsigned long)tid, bufferLocations[tid].begin()->first);
        curPage[tid].init(thisPageName, 20971520);
        curPage[tid].master = true;
        curPage.erase(tid);

        bufferLocations[tid].erase(bufferLocations[tid].begin());
      } else {
        DEBUG_MSG(DLVL_HIGH, "%lu still on first page (%lu - %lu)", myMeta.tracks[tid].keys[0].getNumber(), bufferLocations[tid].begin()->first, bufferLocations[tid].begin()->first + bufferLocations[tid].begin()->second.keyNum - 1);
      }
    }
    return true;
  }

  void inputBuffer::eraseTrackDataPages(unsigned long tid){
    if (!bufferLocations.count(tid)){
      return;
    }
    for (std::map<unsigned long, DTSCPageData>::iterator it = bufferLocations[tid].begin(); it != bufferLocations[tid].end(); it++){
      char thisPageName[NAME_BUFFER_SIZE];
      snprintf(thisPageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, config->getString("streamname").c_str(), tid, it->first);
      curPage[tid].init(thisPageName, 20971520, false, false);
      curPage[tid].master = true;
      curPage.erase(tid);
    }
    bufferLocations.erase(tid);
    metaPages[tid].master = true;
    metaPages.erase(tid);
  }

  void inputBuffer::finish() {
    Input::finish();
    updateMeta();
    if (bufferLocations.size()){
      std::set<unsigned long> toErase;
      for (std::map<unsigned long, std::map<unsigned long, DTSCPageData> >::iterator it = bufferLocations.begin(); it != bufferLocations.end(); it++){
        toErase.insert(it->first);
      }
      for (std::set<unsigned long>::iterator it = toErase.begin(); it != toErase.end(); ++it){
        eraseTrackDataPages(*it);
      }
    }
  }

  /// \triggers 
  /// The `"STREAM_TRACK_REMOVE"` trigger is stream-specific, and is ran whenever a track is fully removed from a live strean buffer. It cannot be cancelled. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// trackID
  /// ~~~~~~~~~~~~~~~
  void inputBuffer::removeUnused(){
    //first remove all tracks that have not been updated for too long
    bool changed = true;
    while (changed){
      changed = false;
      long long unsigned int time = Util::bootSecs();
      long long unsigned int compareFirst = 0xFFFFFFFFFFFFFFFFull;
      long long unsigned int compareLast = 0;
      //for tracks that were updated in the last 5 seconds, get the first and last ms edges.
      for (std::map<unsigned int, DTSC::Track>::iterator it2 = myMeta.tracks.begin(); it2 != myMeta.tracks.end(); it2++){
        if ((time - lastUpdated[it2->first]) > 5){
          continue;
        }
        if (it2->second.lastms > compareLast){
          compareLast = it2->second.lastms;
        }
        if (it2->second.firstms < compareFirst){
          compareFirst = it2->second.firstms;
        }
      }
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        //if not updated for an entire buffer duration, or last updated track and this track differ by an entire buffer duration, erase the track.
        if ((long long int)(time - lastUpdated[it->first]) > (long long int)(bufferTime / 1000) ||
          (compareLast && (long long int)(time - lastUpdated[it->first]) > 5 && (
            (compareLast < it->second.firstms && (long long int)(it->second.firstms - compareLast) > bufferTime)
            ||
            (compareFirst > it->second.lastms && (long long int)(compareFirst - it->second.lastms) > bufferTime)
          ))
        ){
          unsigned int tid = it->first;
          //erase this track
          if ((long long int)(time - lastUpdated[it->first]) > (long long int)(bufferTime / 1000)){
            INFO_MSG("Erasing track %d because not updated for %ds (> %ds)", it->first, (long long int)(time - lastUpdated[it->first]), (long long int)(bufferTime / 1000));
          }else{
            INFO_MSG("Erasing inactive track %u because it was inactive for 5+ seconds and contains data (%us - %us), while active tracks are (%us - %us), which is more than %us seconds apart.", it->first, it->second.firstms / 1000, it->second.lastms / 1000, compareFirst/1000, compareLast/1000, bufferTime / 1000);
          }
          /*LTS-START*/
          if(Triggers::shouldTrigger("STREAM_TRACK_REMOVE")){
            std::string payload = config->getString("streamname")+"\n"+JSON::Value((long long)it->first).asString()+"\n";
            Triggers::doTrigger("STREAM_TRACK_REMOVE", payload, config->getString("streamname"));
          }      
          /*LTS-END*/
          lastUpdated.erase(tid);
          /// \todo Consider replacing with eraseTrackDataPages(it->first)?
          while (bufferLocations[tid].size()){
            char thisPageName[NAME_BUFFER_SIZE];
            snprintf(thisPageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, config->getString("streamname").c_str(), (unsigned long)tid, bufferLocations[tid].begin()->first);
            curPage[tid].init(thisPageName, 20971520);
            curPage[tid].master = true;
            curPage.erase(tid);
            bufferLocations[tid].erase(bufferLocations[tid].begin());
          }
          if (pushLocation.count(it->first)){
            //Reset the userpage, to allow repushing from TS
            IPC::userConnection userConn(pushLocation[it->first]);
            for (int i = 0; i < SIMUL_TRACKS; i++){
              if (userConn.getTrackId(i) == it->first) {
                userConn.setTrackId(i, 0);
                userConn.setKeynum(i, 0);
                break;
              }
            }
            pushLocation.erase(it->first);
          }
          curPageNum.erase(it->first);
          metaPages[it->first].master = true;
          metaPages.erase(it->first);
          activeTracks.erase(it->first);
          myMeta.tracks.erase(it->first);
          changed = true;
          break;
        }
      }
    }
    //find the earliest video keyframe stored
    unsigned int firstVideo = 1;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (it->second.type == "video"){
        if (it->second.firstms < firstVideo || firstVideo == 1){
          firstVideo = it->second.firstms;
        }
      }
    }
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      //non-video tracks need to have a second keyframe that is <= firstVideo
      if (it->second.type != "video"){
        if (it->second.keys.size() < 2 || it->second.keys[1].getTime() > firstVideo){
          continue;
        }
      }
      //Buffer cutting
      while (it->second.keys.size() > 1 && it->second.keys[0].getTime() < cutTime){
        if (!removeKey(it->first)){
          break;
        }
      }
      //Buffer size management
      while (it->second.keys.size() > 1 && (it->second.lastms - it->second.keys[1].getTime()) > bufferTime){
        if (!removeKey(it->first)){
          break;
        }
      }
    }
    updateMeta();
  }

  /// \triggers 
  /// The `"STREAM_TRACK_ADD"` trigger is stream-specific, and is ran whenever a new track is added to a live strean buffer. It cannot be cancelled. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// trackID
  /// ~~~~~~~~~~~~~~~
  void inputBuffer::userCallback(char * data, size_t len, unsigned int id){
    /*LTS-START*/
    //Reload the configuration to make sure we stay up to date with changes through the api
    if (Util::epoch() - lastReTime > 4){
      setup();
    }
    /*LTS-END*/
    //Static variable keeping track of the next temporary mapping to use for a track.
    static int nextTempId = 1001;
    //Get the counter of this user
    char counter = (*(data - 1));
    //Each user can have at maximum SIMUL_TRACKS elements in their userpage.
    IPC::userConnection userConn(data);
    for (int index = 0; index < SIMUL_TRACKS; index++){
      //Get the track id from the current element
      unsigned long value = userConn.getTrackId(index);
      //Skip value 0xFFFFFFFF as this indicates a previously declined track
      if (value == 0xFFFFFFFF){
        continue;
      }
      //Skip value 0 as this indicates an empty track
      if (value == 0){
        continue;
      }

      //If the current value indicates a valid trackid, and it is pushed from this user
      if (pushLocation[value] == data){
        //Check for timeouts, and erase the track if necessary
        if (counter == 126 || counter == 127 || counter == 254 || counter == 255){
          pushLocation.erase(value);
          if (negotiatingTracks.count(value)){
            negotiatingTracks.erase(value);
          }
          if (activeTracks.count(value)){
            updateMeta();
            eraseTrackDataPages(value);
            activeTracks.erase(value);
            bufferLocations.erase(value);
          }
          metaPages[value].master = true;
          metaPages.erase(value);
          continue;
        }
      }
      //Track is set to "New track request", assign new track id and create shared memory page
      //This indicates that the 'current key' part of the element is set to contain the original track id from the pushing process
      if (value & 0x80000000){
        //Set the temporary track id for this item, and increase the temporary value for use with the next track
        unsigned long long tempMapping = nextTempId++;
        //Add the temporary track id to the list of tracks that are currently being negotiated
        negotiatingTracks.insert(tempMapping);
        //Write the temporary id to the userpage element
        userConn.setTrackId(index, tempMapping);
        //Obtain the original track number for the pushing process
        unsigned long originalTrack = userConn.getKeynum(index);
        //Overwrite it with 0xFFFF
        userConn.setKeynum(index, 0xFFFF);
        DEBUG_MSG(DLVL_HIGH, "Incoming track %lu from pushing process %d has now been assigned temporary id %llu", originalTrack, id, tempMapping);
      }

      //The track id is set to the value of a track that we are currently negotiating about
      if (negotiatingTracks.count(value)){
        //If the metadata page for this track is not yet registered, initialize it
        if (!metaPages.count(value) || !metaPages[value].mapped){
          char tempMetaName[NAME_BUFFER_SIZE];
          snprintf(tempMetaName, NAME_BUFFER_SIZE, SHM_TRACK_META, config->getString("streamname").c_str(), value);
          metaPages[value].init(tempMetaName, 8388608, false, false);
        }
        //If this tracks metdata page is not initialize, skip the entire element for now. It will be instantiated later
        if (!metaPages[value].mapped) {
          //remove the negotiation if it has timed out
          if (++negotiationTimeout[value] >= 1000){
            negotiatingTracks.erase(value);
            negotiationTimeout.erase(value);
          }
          continue;
        }

        //The page exist, now we try to read in the metadata of the track

        //Store the size of the dtsc packet to read.
        unsigned int len = ntohl(((int *)metaPages[value].mapped)[1]);
        //Temporary variable, won't be used again
        unsigned int tempForReadingMeta = 0;
        //Read in the metadata through a temporary JSON object
        ///\todo Optimize this part. Find a way to not have to store the metadata in JSON first, but read it from the page immediately
        JSON::Value tempJSONForMeta;
        JSON::fromDTMI((const unsigned char *)metaPages[value].mapped + 8, len, tempForReadingMeta, tempJSONForMeta);
        //Construct a metadata object for the current track
        DTSC::Meta trackMeta(tempJSONForMeta);
        //If the track metadata does not contain the negotiated track, assume the metadata is currently being written, and skip the element for now. It will be instantiated in the next call.
        if (!trackMeta.tracks.count(value)) {
          //remove the negotiation if it has timed out
          if (++negotiationTimeout[value] >= 1000){
            negotiatingTracks.erase(value);
            //Set master to true before erasing the page, because we are responsible for cleaning up unused pages
            metaPages[value].master = true;
            metaPages.erase(value);
            negotiationTimeout.erase(value);
          }
          continue;
        }

        std::string trackIdentifier = trackMeta.tracks.find(value)->second.getIdentifier();
        DEBUG_MSG(DLVL_HIGH, "Attempting colision detection for track %s", trackIdentifier.c_str());
        /*LTS-START*/
        //Get the identifier for the track, and attempt colission detection.
        int collidesWith = -1;
        for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
          //If the identifier of an existing track and the current track match, assume the are the same track and reject the negotiated one.
          ///\todo Maybe switch to a new form of detecting collisions, especially with regards to multiple audio languages and camera angles.
          if (it->second.getIdentifier() == trackIdentifier){
            collidesWith = it->first;
            break;
          }
        }
        /*LTS-END*/
        //Remove the "negotiate" status in either case
        negotiatingTracks.erase(value);
        //Set master to true before erasing the page, because we are responsible for cleaning up unused pages
        metaPages[value].master = true;
        metaPages.erase(value);

        //Check if the track collides, and whether the track it collides with is active.
        if (collidesWith != -1 && activeTracks.count(collidesWith)){/*LTS*/
          //Print a warning message and set the state of the track to rejected.
          WARN_MSG("Collision of temporary track %lu with existing track %d detected. Handling as a new valid track.", value, collidesWith);
          collidesWith = -1;
        }
        /*LTS-START*/
        unsigned long finalMap = collidesWith;
        if (finalMap == -1){
          //No collision has been detected, assign a new final number
          finalMap = (myMeta.tracks.size() ? myMeta.tracks.rbegin()->first : 0) + 1;
          DEBUG_MSG(DLVL_DEVEL, "No colision detected for temporary track %lu from user %u, assigning final track number %lu", value, id, finalMap);
          if(Triggers::shouldTrigger("STREAM_TRACK_ADD")){
            std::string payload = config->getString("streamname")+"\n"+JSON::Value((long long)finalMap).asString()+"\n";
            Triggers::doTrigger("STREAM_TRACK_ADD", payload, config->getString("streamname"));
          }      
        }
        /*LTS-END*/
        //Resume either if we have more than 1 keyframe on the replacement track (assume it was already pushing before the track "dissapeared")
        //or if the firstms of the replacement track is later than the lastms on the existing track
        if (!myMeta.tracks.count(finalMap) || trackMeta.tracks.find(value)->second.keys.size() > 1 || trackMeta.tracks.find(value)->second.firstms >= myMeta.tracks[finalMap].lastms) {
          if (myMeta.tracks.count(finalMap) && myMeta.tracks[finalMap].lastms > 0) {
            INFO_MSG("Resume of track %lu detected, coming from temporary track %lu of user %u", finalMap, value, id);
          } else {
            INFO_MSG("New track detected, assigned track id %lu, coming from temporary track %lu of user %u", finalMap, value, id);
          }
        } else {
          //Otherwise replace existing track
          INFO_MSG("Replacement of track %lu detected, coming from temporary track %lu of user %u", finalMap, value, id);
          myMeta.tracks.erase(finalMap);
          //Set master to true before erasing the page, because we are responsible for cleaning up unused pages
          updateMeta();
          eraseTrackDataPages(value);
          metaPages[finalMap].master = true;
          metaPages.erase(finalMap);
          bufferLocations.erase(finalMap);
        }

        //Register the new track as an active track.
        activeTracks.insert(finalMap);
        //Register the time of registration as initial value for the lastUpdated field, plus an extra 5 seconds just to be sure.
        lastUpdated[finalMap] = Util::bootSecs() + 5;
        //Register the user thats is pushing this element
        pushLocation[finalMap] = data;
        //Initialize the metadata for this track if it was not in place yet.
        if (!myMeta.tracks.count(finalMap)){
          DEBUG_MSG(DLVL_MEDIUM, "Inserting metadata for track number %d", finalMap);
          myMeta.tracks[finalMap] = trackMeta.tracks.begin()->second;
          myMeta.tracks[finalMap].trackID = finalMap;
        }
        //Write the final mapped track number and keyframe number to the user page element
        //This is used to resume pushing as well as pushing new tracks
        userConn.setTrackId(index, finalMap);
        userConn.setKeynum(index, myMeta.tracks[finalMap].keys.size());
        //Update the metadata to reflect all changes
        updateMeta();
      }
      //If the track is active, and this is the element responsible for pushing it
      if (activeTracks.count(value) && pushLocation[value] == data){
        //Open the track index page if we dont have it open yet
        if (!metaPages.count(value) || !metaPages[value].mapped){
          char firstPage[NAME_BUFFER_SIZE];
          snprintf(firstPage, NAME_BUFFER_SIZE, SHM_TRACK_INDEX, config->getString("streamname").c_str(), value);
          metaPages[value].init(firstPage, 8192, false, false);
        }
        if (metaPages[value].mapped){
          //Update the metadata for this track
          updateTrackMeta(value);
        }
      }
    }
  }

  void inputBuffer::updateTrackMeta(unsigned long tNum){
    VERYHIGH_MSG("Updating meta for track %d", tNum);
    //Store a reference for easier access
    std::map<unsigned long, DTSCPageData> & locations = bufferLocations[tNum];

    //First detect all entries on metaPage
    for (int i = 0; i < 8192; i += 8){
      int * tmpOffset = (int *)(metaPages[tNum].mapped + i);
      if (tmpOffset[0] == 0 && tmpOffset[1] == 0){
        continue;
      }
      unsigned long keyNum = ntohl(tmpOffset[0]);
      INSANE_MSG("Page %d detected, with %d keys", keyNum, ntohl(tmpOffset[1]));

      //Add an entry into bufferLocations[tNum] for the pages we haven't handled yet.
      if (!locations.count(keyNum)){
        locations[keyNum].curOffset = 0;
      }
      locations[keyNum].pageNum = keyNum;
      locations[keyNum].keyNum = ntohl(tmpOffset[1]);
    }
    //Since the map is ordered by keynumber, this loop updates the metadata for each page from oldest to newest
    for (std::map<unsigned long, DTSCPageData>::iterator pageIt = locations.begin(); pageIt != locations.end(); pageIt++){
      updateMetaFromPage(tNum, pageIt->first);
    }
    updateMeta();
  }

  void inputBuffer::updateMetaFromPage(unsigned long tNum, unsigned long pageNum){
    VERYHIGH_MSG("Updating meta for track %d page %d", tNum, pageNum);
    DTSCPageData & pageData = bufferLocations[tNum][pageNum];

    //If the current page is over its 8mb "splitting" boundary
    if (pageData.curOffset > (8 * 1024 * 1024)){
      //And the last keyframe in the parsed metadata is further in the stream than this page
      if (pageData.pageNum + pageData.keyNum < myMeta.tracks[tNum].keys.rbegin()->getNumber()){
        //Assume the entire page is already parsed
        return;
      }
    }

    //Otherwise open and parse the page

    //Open the page if it is not yet open
    if (!curPageNum.count(tNum) || curPageNum[tNum] != pageNum || !curPage[tNum].mapped){
      //DO NOT ERASE THE PAGE HERE, master is not set to true
      curPageNum.erase(tNum);
      char nextPageName[NAME_BUFFER_SIZE];
      snprintf(nextPageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, config->getString("streamname").c_str(), tNum, pageNum);
      curPage[tNum].init(nextPageName, 20971520);
      //If the page can not be opened, stop here
      if (!curPage[tNum].mapped){
        WARN_MSG("Could not open page: %s", nextPageName);
        return;
      }
      curPageNum[tNum] = pageNum;
    }


    DTSC::Packet tmpPack;
    if (!curPage[tNum].mapped[pageData.curOffset]){
      VERYHIGH_MSG("No packet on page %lu for track %lu, waiting...", pageNum, tNum);
      return;
    }
    tmpPack.reInit(curPage[tNum].mapped + pageData.curOffset, 0);
    //No new data has been written on the page since last update
    if (!tmpPack){
      return;
    }
    lastUpdated[tNum] = Util::bootSecs();
    while (tmpPack){
      //Update the metadata with this packet
      myMeta.update(tmpPack, segmentSize);/*LTS*/
      //Set the first time when appropriate
      if (pageData.firstTime == 0){
        pageData.firstTime = tmpPack.getTime();
      }
      //Update the offset on the page with the size of the current packet
      pageData.curOffset += tmpPack.getDataLen();
      //Attempt to read in the next packet
      tmpPack.reInit(curPage[tNum].mapped + pageData.curOffset, 0);
    }
  }

  bool inputBuffer::setup(){
    lastReTime = Util::epoch(); /*LTS*/
    std::string strName = config->getString("streamname");
    Util::sanitizeName(strName);
    strName = strName.substr(0, (strName.find_first_of("+ ")));
    IPC::sharedPage serverCfg("!mistConfig", DEFAULT_CONF_PAGE_SIZE, false, false); ///< Contains server configuration and capabilities
    IPC::semaphore configLock("!mistConfLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
    configLock.wait();
    DTSC::Scan streamCfg = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("streams").getMember(strName);
    long long tmpNum;

    //if stream is configured and setting is present, use it, always
    if (streamCfg && streamCfg.getMember("DVR")){
      tmpNum = streamCfg.getMember("DVR").asInt();
    } else {
      if (streamCfg){
        //otherwise, if stream is configured use the default
        tmpNum = config->getOption("bufferTime", true)[0u].asInt();
      } else {
        //if not, use the commandline argument
        tmpNum = config->getOption("bufferTime").asInt();
      }
    }
    if (tmpNum < 1000){tmpNum = 1000;}
    //if the new value is different, print a message and apply it
    if (bufferTime != tmpNum){
      DEBUG_MSG(DLVL_DEVEL, "Setting bufferTime from %u to new value of %lli", bufferTime, tmpNum);
      bufferTime = tmpNum;
    }

    /*LTS-START*/
    //if stream is configured and setting is present, use it, always
    if (streamCfg && streamCfg.getMember("cut")){
      tmpNum = streamCfg.getMember("cut").asInt();
    } else {
      if (streamCfg){
        //otherwise, if stream is configured use the default
        tmpNum = config->getOption("cut", true)[0u].asInt();
      } else {
        //if not, use the commandline argument
        tmpNum = config->getOption("cut").asInt();
      }
    }
    //if the new value is different, print a message and apply it
    if (cutTime != tmpNum){
      DEBUG_MSG(DLVL_DEVEL, "Setting cutTime from %u to new value of %lli", cutTime, tmpNum);
      cutTime = tmpNum;
    }


    //if stream is configured and setting is present, use it, always
    if (streamCfg && streamCfg.getMember("segmentsize")){
      tmpNum = streamCfg.getMember("segmentsize").asInt();
    } else {
      if (streamCfg){
        //otherwise, if stream is configured use the default
        tmpNum = config->getOption("segmentsize", true)[0u].asInt();
      } else {
        //if not, use the commandline argument
        tmpNum = config->getOption("segmentsize").asInt();
      }
    }
    //if the new value is different, print a message and apply it
    if (segmentSize != tmpNum){
      DEBUG_MSG(DLVL_DEVEL, "Setting segmentSize from %u to new value of %lli", segmentSize, tmpNum);
      segmentSize = tmpNum;
    }

    /*
    //if stream is configured and setting is present, use it, always
    std::string rec;
    if (streamCfg && streamCfg.getMember("record")){
      rec = streamCfg.getMember("record").asInt();
    } else {
      if (streamCfg){
        //otherwise, if stream is configured use the default
        rec = config->getOption("record", true)[0u].asString();
      } else {
        //if not, use the commandline argument
        rec = config->getOption("record").asString();
      }
    }
    //if the new value is different, print a message and apply it
    if (recName != rec){
      //close currently recording file, for we should open a new one
      DEBUG_MSG(DLVL_DEVEL, "Stopping recording of %s to %s", config->getString("streamname").c_str(), recName.c_str());
      recFile.close();
      recMeta.tracks.clear();
      recName = rec;
    }
    if (recName != "" && !recFile.is_open()){
      DEBUG_MSG(DLVL_DEVEL, "Starting recording of %s to %s", config->getString("streamname").c_str(), recName.c_str());
      recFile.open(recName.c_str());
      if (recFile.fail()){
        DEBUG_MSG(DLVL_DEVEL, "Error occured during record opening: %s", strerror(errno));
      }
      recBpos = 0;
    }
    */

    /*LTS-END*/
    configLock.post();
    configLock.close();
    return true;
  }

  bool inputBuffer::readHeader(){
    return true;
  }

  void inputBuffer::getNext(bool smart){}

  void inputBuffer::seek(int seekTime){}

  void inputBuffer::trackSelect(std::string trackSpec){}
}



