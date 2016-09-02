#include <mist/stream.h>
#include <mist/json.h>
#include <mist/auth.h>
#include <mist/encode.h>
#include <mist/bitfields.h>
#include <cstdlib>
#include "io.h"

namespace Mist {
  Util::Config * InOutBase::config = NULL;
  ///Opens a shared memory page for the stream metadata.
  ///
  ///Assumes myMeta contains the metadata to write.
  void InOutBase::initiateMeta() {
    VERYHIGH_MSG("initiateMeta for stream %s", streamName.c_str());
    //Open the page for the metadata
    char pageName[NAME_BUFFER_SIZE];
    snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_INDEX, streamName.c_str());
    if (myMeta.live){
      nProxy.metaPages[0].init(pageName, DEFAULT_STRM_PAGE_SIZE, true);
    }else{
      nProxy.metaPages[0].init(pageName, myMeta.getSendLen(), true);
    }
    //Make sure we don't delete it on accident
    nProxy.metaPages[0].master = false;

    //Write the metadata to the page
    myMeta.writeTo(nProxy.metaPages[0].mapped);

  }

  bool InOutBase::bufferStart(unsigned long tid, unsigned long pageNumber) {
    VERYHIGH_MSG("bufferStart for stream %s, track %lu, page %lu", streamName.c_str(), tid, pageNumber);
    //Initialize the stream metadata if it does not yet exist
    if (!nProxy.metaPages.count(0)) {
      initiateMeta();
    }
    //If we are a stand-alone player skip track negotiation, as there will be nothing to negotiate with.
    if (standAlone) {
      if (!nProxy.trackMap.count(tid)) {
        nProxy.trackMap[tid] = tid;
      }
    }
    //Negotiate the requested track if needed.
    return nProxy.bufferStart(tid, pageNumber, myMeta);
  }

  ///Starts the buffering of a new page.
  ///
  ///Does not do any actual buffering, just sets the right bits for buffering to go right.
  ///
  ///Buffering itself is done by bufferNext().
  ///\param tid The trackid of the page to start buffering
  ///\param pageNumber The number of the page to start buffering
  bool negotiationProxy::bufferStart(unsigned long tid, unsigned long pageNumber, DTSC::Meta & myMeta) {
    //Negotiate the requested track if needed.
    continueNegotiate(tid, myMeta);

    //If the negotation state for this track is not 'Accepted', stop buffering this page, maybe try again later.
    if (trackState[tid] != FILL_ACC) {
      ///\return false if the track has not been accepted (yet)
      return false;
    }
    //If the track is accepted, we will have a mapped tid
    unsigned long mapTid = trackMap[tid];

    //Before we start a new page, make sure we can be heard by the buffer about this.
    //Otherwise, it might linger forever as a nasty data leak.
    //Nobody likes nasty data leaks.
    {
      char pageName[NAME_BUFFER_SIZE];
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_TRACK_INDEX, streamName.c_str(), mapTid);
      IPC::sharedPage checkPage(pageName, SHM_TRACK_INDEX_SIZE, false, false);
      if (!checkPage.mapped){
        WARN_MSG("Buffer deleted %s@%lu (%s) index. Re-negotiating...", streamName.c_str(), mapTid, myMeta.tracks[tid].codec.c_str());
        trackState.erase(tid);
        trackMap.erase(tid);
        trackOffset.erase(tid);
        pagesByTrack.erase(tid);
        metaPages.erase(tid);
        curPageNum.erase(tid);
        curPage.erase(tid);
        return bufferStart(tid, pageNumber, myMeta);
      }
    }

    //If we are currently buffering a page, abandon it completely and print a message about this
    //This page will NEVER be deleted, unless we open it again later.
    if (curPage.count(tid)) {
      WARN_MSG("Abandoning current page (%lu) for track %lu~>%lu", curPageNum[tid], tid, mapTid);
      curPage.erase(tid);
      curPageNum.erase(tid);
    }

    //If this is not a valid page number on this track, stop buffering this page.
    if (!pagesByTrack[tid].count(pageNumber)){
      WARN_MSG("Aborting page buffer start: %lu is not a valid page number on track %lu~>%lu.", pageNumber, tid, mapTid);
      std::stringstream test;
      for (std::map<unsigned long, DTSCPageData>::iterator it = pagesByTrack[tid].begin(); it != pagesByTrack[tid].end(); it++){
        test << it->first << " ";
      }
      INFO_MSG("Valid page numbers: %s", test.str().c_str());
      ///\return false if the pagenumber is not valid for this track
      return false;
    }

    //If the page is already buffered, ignore this request
    if (isBuffered(tid, pageNumber)) {
      INFO_MSG("Page %lu on track %lu~>%lu already buffered", pageNumber, tid, mapTid);
      ///\return false if the page was already buffered.
      return false;
    }

    //Open the correct page for the data
    char pageId[NAME_BUFFER_SIZE];
    snprintf(pageId, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), mapTid, pageNumber);
    int pageSize = pagesByTrack[tid][pageNumber].dataSize;
#ifdef __CYGWIN__
    pageSize = 26 * 1024 * 1024;
#endif
    std::string pageName(pageId);
    curPage[tid].init(pageName, pageSize, true);
    //Make sure the data page is not destroyed when we are done buffering it later on.
    curPage[tid].master = false;
    //Store the pagenumber of the currently buffer page
    curPageNum[tid] = pageNumber;

    //Initialize the bookkeeping entry, and set the current offset to 0, to allow for using it in bufferNext()
    pagesByTrack[tid][pageNumber].curOffset = 0;

    HIGH_MSG("Start buffering page %lu on track %lu~>%lu successful", pageNumber, tid, mapTid);


    if (myMeta.live){
      //Register this page on the meta page
      //NOTE: It is important that this only happens if the stream is live....
      bool inserted = false;
      for (int i = 0; i < 1024; i++) {
        int * tmpOffset = (int *)(metaPages[tid].mapped + (i * 8));
        if ((tmpOffset[0] == 0 && tmpOffset[1] == 0)) {
          tmpOffset[0] = htonl(curPageNum[tid]);
          tmpOffset[1] = htonl(1000);
          inserted = true;
          break;
        }
      }
      if (!inserted){
        FAIL_MSG("Could not insert page in track index. Aborting.");
        return false;
      }
    }

    ///\return true if everything was successful
    return true;
  }

  ///Removes a fully buffered page
  ///
  ///Does not do anything if the process is not standalone, in this case the master process will have an overloaded version of this function.
  ///\param tid The trackid to remove the page from
  ///\param pageNumber The number of the page to remove
  void InOutBase::bufferRemove(unsigned long tid, unsigned long pageNumber) {
    if (!standAlone) {
      //A different process will handle this for us
      return;
    }
    unsigned long mapTid = nProxy.trackMap[tid];

    DEBUG_MSG(DLVL_HIGH, "Removing page %lu on track %lu~>%lu from the corresponding metaPage", pageNumber, tid, mapTid);
    int i = 0;
    for (; i < 1024; i++) {
      int * tmpOffset = (int *)(nProxy.metaPages[tid].mapped + (i * 8));
      if (ntohl(tmpOffset[0]) == pageNumber) {
        tmpOffset[0] = 0;
        tmpOffset[1] = 0;
        break;
      }
    }
    if (i == 1024){
      FAIL_MSG("Could not erase page %lu for track %lu->%lu stream %s from track index!", pageNumber, tid, mapTid, streamName.c_str());
    }

    if (!nProxy.pagesByTrack.count(tid)){
      // If there is no pagesByTrack entry, the pages are managed in local code and not through io.cpp (e.g.: MistInBuffer)
      return;
    }
    //If the given pagenumber is not a valid page on this track, do nothing
    if (!nProxy.pagesByTrack[tid].count(pageNumber)){
      INFO_MSG("Can't remove page %lu on track %lu~>%lu as it is not a valid page number.", pageNumber, tid, mapTid);
      return;
    }
    //Open the correct page
    char pageId[NAME_BUFFER_SIZE];
    snprintf(pageId, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), mapTid, pageNumber);
    std::string pageName(pageId);
    IPC::sharedPage toErase;
#ifdef __CYGWIN__
    toErase.init(pageName, 26 * 1024 * 1024, false);
#else
    toErase.init(pageName, nProxy.pagesByTrack[tid][pageNumber].dataSize, false);
#endif
    //Set the master flag so that the page will be destroyed once it leaves scope
#if defined(__CYGWIN__) || defined(_WIN32)
    IPC::releasePage(pageName);
#endif
    toErase.master = true;
    //Remove the page from the tracks index page
    //Leaving scope here, the page will now be destroyed
  }


  ///Checks whether a key is buffered
  ///\param tid The trackid on which to locate the key
  ///\param keyNum The number of the keyframe to find
  bool negotiationProxy::isBuffered(unsigned long tid, unsigned long keyNum) {
    ///\return The result of bufferedOnPage(tid, keyNum)
    return bufferedOnPage(tid, keyNum);
  }

  ///Returns the pagenumber where this key is buffered on
  ///\param tid The trackid on which to locate the key
  ///\param keyNum The number of the keyframe to find
  unsigned long negotiationProxy::bufferedOnPage(unsigned long tid, unsigned long keyNum) {
    //Check whether the track is accepted
    if (!trackMap.count(tid) || !metaPages.count(tid) || !metaPages[tid].mapped) {
      ///\return 0 if the page has not been mapped yet
      return 0;
    }
    //Loop over the index page
    int len = metaPages[tid].len / 8;
    for (int i = 0; i < len; ++i) {
      int * tmpOffset = (int *)(metaPages[tid].mapped + (i * 8));
      unsigned int keyAmount = ntohl(tmpOffset[1]);
      if (keyAmount == 0){continue;}
      //Check whether the key is on this page
      unsigned int pageNum = ntohl(tmpOffset[0]);
      if (pageNum <= keyNum && keyNum < pageNum + keyAmount) {
        return pageNum;
      }
    }
    return 0;
  }

  ///Buffers the next packet on the currently opened page
  ///\param pack The packet to buffer
  void InOutBase::bufferNext(JSON::Value & pack) {
    std::string packData = pack.toNetPacked();
    DTSC::Packet newPack(packData.data(), packData.size());
    ///\note Internally calls bufferNext(DTSC::Packet & pack)
    nProxy.bufferNext(newPack, myMeta);
  }

  ///Buffers the next packet on the currently opened page
  ///\param pack The packet to buffer
  void InOutBase::bufferNext(DTSC::Packet & pack) {
    nProxy.bufferNext(pack, myMeta);
  }

  void negotiationProxy::bufferNext(DTSC::Packet & pack, DTSC::Meta & myMeta) {
    static bool multiWrong = false;
    //Save the trackid of the track for easier access
    unsigned long tid = pack.getTrackId();
    if (myMeta.live && pack.getTime() > 0xFFFF0000 && !myMeta.tracks[tid].lastms){
      return;//ignore bullshit timestamps
    }
    if (myMeta.live && pack.getTime() < myMeta.tracks[tid].lastms){
      DEBUG_MSG(multiWrong?DLVL_HIGH:DLVL_WARN, "Wrong order on track %lu ignored: %lu < %lu", tid, pack.getTime(), myMeta.tracks[tid].lastms);
      multiWrong = true;
      return;
    }
    unsigned long mapTid = trackMap[tid];
    //Do nothing if no page is opened for this track
    if (!curPage.count(tid)) {
      INFO_MSG("Trying to buffer a packet on track %lu~>%lu, but no page is initialized", tid, mapTid);
      return;
    }
    multiWrong = false;
    IPC::sharedPage & myPage = curPage[tid];
    DTSCPageData & pageData = pagesByTrack[tid][curPageNum[tid]];
    //Save the current write position
    size_t curOffset = pageData.curOffset;
    //Do nothing when there is not enough free space on the page to add the packet.
    if (pageData.dataSize - curOffset < pack.getDataLen()) {
      FAIL_MSG("Trying to buffer a packet on page %lu for track %lu~>%lu, but we have a size mismatch. The packet is %d bytes long, so won't fit at offset %lu on a page of %llu bytes!", curPageNum[tid], tid, mapTid, pack.getDataLen(), curOffset, pageData.dataSize);
      return;
    }

    //Brain melt starts here

    //First memcpy only the payload to the destination
    //Leaves the 20 bytes inbetween empty to ensure the data is not accidentally read before it is complete
    memcpy(myPage.mapped + curOffset + 20, pack.getData() + 20, pack.getDataLen() - 20);
    //Copy the remaing values in reverse order:
    //8 byte timestamp
    memcpy(myPage.mapped + curOffset + 12, pack.getData() + 12, 8);
    //The mapped track id
    ((int *)(myPage.mapped + curOffset + 8))[0] = htonl(mapTid);
    int size = Bit::btohl(pack.getData() + 4);
    //Write the size
    Bit::htobl(myPage.mapped + curOffset + 4, size);
    //write the 'DTP2' bytes to conclude the packet and allow for reading it
    memcpy(myPage.mapped + curOffset, pack.getData(), 4);


    if (myMeta.live){
      //Update the metadata
      DTSC::Packet updatePack(myPage.mapped + curOffset, size + 8, true);
      myMeta.update(updatePack);
    }

    //End of brain melt
    pageData.curOffset += size + 8;
  }

  ///Wraps up the buffering of a shared memory data page
  ///
  ///Registers the data page on the track index page as well
  ///\param tid The trackid of the page to finalize
  void InOutBase::bufferFinalize(unsigned long tid) {
    nProxy.bufferFinalize(tid, myMeta);
  }

  void negotiationProxy::bufferFinalize(unsigned long tid, DTSC::Meta & myMeta){
    unsigned long mapTid = trackMap[tid];
    //If no page is open, do nothing
    if (!curPage.count(tid)) {
      INFO_MSG("Trying to finalize the current page on track %lu~>%lu, but no page is initialized", tid, mapTid);
      return;
    }

    //Keep track of registering the page on the track's index page
    bool inserted = false;
    int lowest = 0;
    for (int i = 0; i < 1024; i++) {
      int * tmpOffset = (int *)(metaPages[tid].mapped + (i * 8));
      int keyNum = ntohl(tmpOffset[0]);
      int keyAmount = ntohl(tmpOffset[1]);
      if (!inserted){
        if (myMeta.live){
          if(keyNum == curPageNum[tid] && keyAmount == 1000){
            tmpOffset[1] = htonl(pagesByTrack[tid][curPageNum[tid]].keyNum);
            inserted = true;
          }
        }else{
          //in case of vod, insert at the first "empty" spot
          if(keyNum == 0){
            tmpOffset[0] = htonl(curPageNum[tid]);
            tmpOffset[1] = htonl(pagesByTrack[tid][curPageNum[tid]].keyNum);
            inserted = true;
          }
        }
      }
      keyNum = ntohl(tmpOffset[0]);
      if (!keyNum) continue;
      if (!lowest || keyNum < lowest){
        lowest = keyNum;
      }
    }

#if defined(__CYGWIN__) || defined(_WIN32)
    static int wipedAlready = 0;
    if (lowest && lowest > wipedAlready + 1){
      for (int curr = wipedAlready + 1; curr < lowest; ++curr){
        char pageId[NAME_BUFFER_SIZE];
        snprintf(pageId, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), mapTid, curr);
        IPC::releasePage(std::string(pageId));
      }
    }
#endif

    //Print a message about registering the page or not.
    if (!inserted) {
      FAIL_MSG("Can't register page %lu on the metaPage of track %lu~>%lu, No empty spots left within 'should be' amount of slots", curPageNum[tid], tid, mapTid);
    } else {
      HIGH_MSG("Succesfully registered page %lu on the metaPage of track %lu~>%lu.", curPageNum[tid], tid, mapTid);
    }
    //Close our link to the page. This will NOT destroy the shared page, as we've set master to false upon construction
#if defined(__CYGWIN__) || defined(_WIN32)
    IPC::preservePage(curPage[tid].name);
#endif
    curPage.erase(tid);
    curPageNum.erase(tid);
  }

  ///Buffers a live packet to a page.
  ///
  ///Handles both buffering and creation of new pages
  ///
  ///Initiates/continues negotiation with the buffer as well
  ///\param packet The packet to buffer
  void InOutBase::bufferLivePacket(JSON::Value & packet) {
    DTSC::Packet realPacket;
    realPacket.genericFill(packet["time"].asInt(), packet["offset"].asInt(), packet["trackid"].asInt(), packet["data"].asStringRef().c_str(), packet["data"].asStringRef().size(), packet["bpos"].asInt(), packet["keyframe"].asInt());
    bufferLivePacket(realPacket);
  }


  ///Buffers a live packet to a page.
  ///
  ///Handles both buffering and creation of new pages
  ///
  ///Initiates/continues negotiation with the buffer as well
  ///\param packet The packet to buffer
  void InOutBase::bufferLivePacket(DTSC::Packet & packet){
    nProxy.bufferLivePacket(packet, myMeta);
  }

  void negotiationProxy::bufferLivePacket(DTSC::Packet & packet, DTSC::Meta & myMeta){
    myMeta.vod = false;
    myMeta.live = true;
    //Store the trackid for easier access
    unsigned long tid = packet.getTrackId();
    //Do nothing if the trackid is invalid
    if (!tid) {
      INFO_MSG("Packet without trackid");
      return;
    }
    //If the track is not negotiated yet, start the negotiation
    if (!trackState.count(tid)) {
      continueNegotiate(tid, myMeta);
    }
    //If the track is declined, stop here
    if (trackState[tid] == FILL_DEC) {
      INFO_MSG("Track %lu Declined", tid);
      preBuffer[tid].clear();
      return;
    }
    //Not accepted yet? Buffer.
    if (trackState[tid] != FILL_ACC) {
      preBuffer[tid].push_back(packet);
    }else{
      while (preBuffer[tid].size()){
        bufferSinglePacket(preBuffer[tid].front(), myMeta);
        preBuffer[tid].pop_front();
      }
      bufferSinglePacket(packet, myMeta);
    }
  }

  void negotiationProxy::bufferSinglePacket(DTSC::Packet & packet, DTSC::Meta & myMeta){
    //Store the trackid for easier access
    unsigned long tid = packet.getTrackId();
    //This update needs to happen whether the track is accepted or not.
    bool isKeyframe = false;
    if (myMeta.tracks[tid].type == "video") {
      if (packet.hasMember("keyframe") && packet.getFlag("keyframe")) {
        isKeyframe = true;
      }
    } else {
      if (!pagesByTrack.count(tid) || pagesByTrack[tid].size() == 0) {
        //Assume this is the first packet on the track
        isKeyframe = true;
      } else {
        unsigned long lastKey = pagesByTrack[tid].rbegin()->second.lastKeyTime;
        if (packet.getTime() - lastKey > 5000) {
          isKeyframe = true;
        }
      }
    }
    //Determine if we need to open the next page
    int nextPageNum = -1;
    if (isKeyframe && trackState[tid] == FILL_ACC) {
      //If there is no page, create it
      if (!pagesByTrack.count(tid) || pagesByTrack[tid].size() == 0) {
        nextPageNum = 1;
        pagesByTrack[tid][1].dataSize = DEFAULT_DATA_PAGE_SIZE;//Initialize op 25mb
        pagesByTrack[tid][1].pageNum = 1;
        pagesByTrack[tid][1].firstTime = packet.getTime();
      }
      //Take the last allocated page
      std::map<unsigned long, DTSCPageData>::reverse_iterator tmpIt = pagesByTrack[tid].rbegin();
      //Compare on 8 mb boundary
      if (tmpIt->second.curOffset > FLIP_DATA_PAGE_SIZE || packet.getTime() - tmpIt->second.firstTime > FLIP_TARGET_DURATION) { 
        //Create the book keeping data for the new page
        nextPageNum = tmpIt->second.pageNum + tmpIt->second.keyNum;
        HIGH_MSG("We should go to next page now, transition from %lu to %d", tmpIt->second.pageNum, nextPageNum);
        pagesByTrack[tid][nextPageNum].dataSize = DEFAULT_DATA_PAGE_SIZE;
        pagesByTrack[tid][nextPageNum].pageNum = nextPageNum;
        pagesByTrack[tid][nextPageNum].firstTime = packet.getTime();
      }
      pagesByTrack[tid].rbegin()->second.lastKeyTime = packet.getTime();
      pagesByTrack[tid].rbegin()->second.keyNum++;
    }
    //Set the pageNumber if it has not been set yet
    if (nextPageNum == -1) {
      if (curPageNum.count(tid)) {
        nextPageNum = curPageNum[tid];
      }else{
        nextPageNum = 1;
      }
    }
    //If we have no pages by track, we have not received a starting keyframe yet. Drop this packet.
    if (!pagesByTrack.count(tid) || pagesByTrack[tid].size() == 0){
      return;
    }

    //Check if the correct page is opened
    if (!curPageNum.count(tid) || nextPageNum != curPageNum[tid]) {
      if (curPageNum.count(tid)) {
        //Close the currently opened page when it exists
        bufferFinalize(tid, myMeta);
      }
      //Open the new page
      bufferStart(tid, nextPageNum, myMeta);
    }
    //Buffer the packet
    bufferNext(packet, myMeta);
  }

  void InOutBase::continueNegotiate(unsigned long tid, bool quickNegotiate) {
    nProxy.continueNegotiate(tid, myMeta, quickNegotiate);
  }

  void negotiationProxy::continueNegotiate(unsigned long tid, DTSC::Meta & myMeta, bool quickNegotiate) {
    if (!tid) {
      return;
    }
    if (userClient.getData()){
      userClient.keepAlive();
    }
    if (trackMap.count(tid) && !trackState.count(tid)) {
      //If the trackmap has been set manually, don't negotiate
      HIGH_MSG("TrackMap manual, not negotiating track IDs");
      trackState[tid] = FILL_ACC;
      char pageName[NAME_BUFFER_SIZE];
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_TRACK_INDEX, streamName.c_str(), tid);
      metaPages[tid].init(pageName, SHM_TRACK_INDEX_SIZE, true);
      metaPages[tid].master = false;
      return;
    }
    //dont try to re-negoiate existing tracks, if this is what you want, remove the tid from the trackState before calling this function
    if (trackState.count(tid) && (trackState[tid] == FILL_DEC || trackState[tid] == FILL_ACC)) {
      return;
    }
    if (!trackOffset.count(tid)) {
      if (trackOffset.size() > SIMUL_TRACKS) {
        WARN_MSG("Trackoffset too high");
        return;
      }
      //Find a free offset for the new track
      for (int i = 0; i < SIMUL_TRACKS; i++) {
        bool isFree = true;
        for (std::map<unsigned long, unsigned long>::iterator it = trackOffset.begin(); it != trackOffset.end(); it++) {
          if (it->second == i) {
            isFree = false;
            break;
          }
        }
        if (isFree) {
          trackOffset[tid] = i;
          break;
        }
      }
    }
    //Now we either returned or the track has an offset for the user page.
    //Get the data from the userPage
    if (!userClient.getData()){
      char userPageName[100];
      sprintf(userPageName, SHM_USERS, streamName.c_str());
      userClient = IPC::sharedClient(userPageName, PLAY_EX_SIZE, true);
    }
    char * tmp = userClient.getData();
    if (!tmp) {
      DEBUG_MSG(DLVL_FAIL, "Failed to negotiate for incoming track %lu, there does not seem to be a connection with the buffer", tid);
      return;
    }
    unsigned long offset = 6 * trackOffset[tid];
    //If we have a new track to negotiate
    if (!trackState.count(tid)) {
      memset(tmp + offset, 0, 4);
      if (quickNegotiate){
        unsigned long finalTid = tid;
        unsigned short firstPage = 1;
        MEDIUM_MSG("Buffer has indicated that incoming track %lu should start writing on track %lu, page %lu", tid, finalTid, firstPage);
        trackMap[tid] = finalTid;
        if (myMeta.tracks.count(finalTid) && myMeta.tracks[finalTid].lastms){
          myMeta.tracks[finalTid].lastms = 0;
        }
        trackState[tid] = FILL_ACC;



        char pageName[NAME_BUFFER_SIZE];
        snprintf(pageName, NAME_BUFFER_SIZE, SHM_TRACK_META, streamName.c_str(), finalTid);
        metaPages[tid].init(pageName, 8 * 1024 * 1024, true);
        metaPages[tid].master = false;
        DTSC::Meta tmpMeta;
        tmpMeta.tracks[finalTid] = myMeta.tracks[tid];
        tmpMeta.tracks[finalTid].trackID = finalTid;
        JSON::Value tmpVal = tmpMeta.toJSON();
        std::string tmpStr = tmpVal.toNetPacked();
        memcpy(metaPages[tid].mapped, tmpStr.data(), tmpStr.size());





        snprintf(pageName, NAME_BUFFER_SIZE, SHM_TRACK_INDEX, streamName.c_str(), finalTid);
        metaPages[tid].init(pageName, SHM_TRACK_INDEX_SIZE, true);
        metaPages[tid].master = false;
        Bit::htobl(tmp + offset, finalTid | 0xC0000000);
        Bit::htobs(tmp + offset + 4, firstPage);
      }else{
        INFO_MSG("Starting negotiation for incoming track %lu, at offset %lu", tid, trackOffset[tid]);
        memset(tmp + offset, 0, 4);
        tmp[offset] = 0x80;
        tmp[offset + 4] = ((tid >> 8) & 0xFF);
        tmp[offset + 5] = (tid & 0xFF);
        trackState[tid] = FILL_NEW;
      }
      return;
    }
    #if defined(__CYGWIN__) || defined(_WIN32)
    static std::map<unsigned long, std::string> preservedTempMetas;
    #endif
    switch (trackState[tid]) {
      case FILL_NEW: {
          unsigned long newTid = ((long)(tmp[offset]) << 24) | ((long)(tmp[offset + 1]) << 16) | ((long)(tmp[offset + 2]) << 8) | tmp[offset + 3];
          INSANE_MSG("NewTid: %0.8lX", newTid);
          if (newTid == 0x80000000u) {
            INSANE_MSG("Breaking because not set yet");
            break;
          }
          HIGH_MSG("Track %lu temporarily mapped to %lu", tid, newTid);

          char pageName[NAME_BUFFER_SIZE];
          snprintf(pageName, NAME_BUFFER_SIZE, SHM_TRACK_META, streamName.c_str(), newTid);
          metaPages[tid].init(pageName, 8 * 1024 * 1024, true);
          metaPages[tid].master = false;
          DTSC::Meta tmpMeta;
          tmpMeta.tracks[newTid] = myMeta.tracks[tid];
          tmpMeta.tracks[newTid].trackID = newTid;
          JSON::Value tmpVal = tmpMeta.toJSON();
          std::string tmpStr = tmpVal.toNetPacked();
          memcpy(metaPages[tid].mapped, tmpStr.data(), tmpStr.size());
          HIGH_MSG("Temporary metadata written for incoming track %lu, handling as track %lu", tid, newTid);
          //Not actually removing the page, because we set master to false
          #if defined(__CYGWIN__) || defined(_WIN32)
          IPC::preservePage(pageName);
          preservedTempMetas[tid] = pageName;
          #endif
          metaPages.erase(tid);
          trackState[tid] = FILL_NEG;
          trackMap[tid] = newTid;
          break;
        }
      case FILL_NEG: {
          unsigned long finalTid = ((long)(tmp[offset]) << 24) | ((long)(tmp[offset + 1]) << 16) | ((long)(tmp[offset + 2]) << 8) | tmp[offset + 3];
          unsigned long firstPage = firstPage = ((long)(tmp[offset + 4]) << 8) | tmp[offset + 5];
          if (firstPage == 0xFFFF) {
            HIGH_MSG("Negotiating, but firstPage not yet set, waiting for buffer");
            break;
          }
          #if defined(__CYGWIN__) || defined(_WIN32)
          IPC::releasePage(preservedTempMetas[tid]);
          preservedTempMetas.erase(tid);
          #endif
          if (finalTid == 0xFFFFFFFF) {
            WARN_MSG("Buffer has declined incoming track %lu", tid);
            memset(tmp + offset, 0, 6);
            trackState[tid] = FILL_DEC;
            trackMap.erase(tid);
            break;
          }
          //Reinitialize so we can be sure we got the right values here
          finalTid = ((long)(tmp[offset]) << 24) | ((long)(tmp[offset + 1]) << 16) | ((long)(tmp[offset + 2]) << 8) | tmp[offset + 3];
          firstPage = ((long)(tmp[offset + 4]) << 8) | tmp[offset + 5];
          if (finalTid == 0xFFFFFFFF) {
            WARN_MSG("Buffer has declined incoming track %lu", tid);
            memset(tmp + offset, 0, 6);
            trackState[tid] = FILL_DEC;
            trackMap.erase(tid);
            break;
          }

          MEDIUM_MSG("Buffer says %s:%lu should start writing on track %lu, page %lu", streamName.c_str(), tid, finalTid, firstPage);
          trackMap[tid] = finalTid;
          if (myMeta.tracks.count(finalTid) && myMeta.tracks[finalTid].lastms){
            myMeta.tracks[finalTid].lastms = 0;
          }
          trackState[tid] = FILL_ACC;
          char pageName[NAME_BUFFER_SIZE];
          snprintf(pageName, NAME_BUFFER_SIZE, SHM_TRACK_INDEX, streamName.c_str(), finalTid);
          metaPages[tid].init(pageName, SHM_TRACK_INDEX_SIZE, true);
          metaPages[tid].master = false;
          break;
        }
      default:
        //We can't get here because we catch this case in the beginning of the function,
        //this case surpresses a compiler warning
        break;
    }
  }

}
