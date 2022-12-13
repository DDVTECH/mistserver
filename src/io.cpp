#include "io.h"
#include <cstdlib>
#include <mist/auth.h>
#include <mist/bitfields.h>
#include <mist/encode.h>
#include <mist/http_parser.h>
#include <mist/json.h>
#include <mist/langcodes.h> //LTS
#include <mist/stream.h>
#include <mist/h264.h>
#include <mist/config.h>

namespace Mist{
  InOutBase::InOutBase() : M(meta){}

  /// Returns the ID of the main selected track, or 0 if no tracks are selected.
  /// The main track is the first video track, if any, and otherwise the first other track.
  /// Returns INVALID_TRACK_ID if there are no valid selected tracks.
  /// Refreshes the metadata to make sure we don't return unloaded tracks.
  size_t InOutBase::getMainSelectedTrack(){
    if (!userSelect.size()){return INVALID_TRACK_ID;}
    size_t bestSoFar = INVALID_TRACK_ID;
    meta.reloadReplacedPagesIfNeeded();
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      if (meta.trackValid(it->first)){
        if (meta.getType(it->first) == "video"){return it->first;}
        bestSoFar = it->first;
      }
    }
    return bestSoFar;
  }

  /// Starts the buffering of a new page.
  ///
  /// Does not do any actual buffering, just sets the right bits for buffering to go right.
  ///
  /// Buffering itself is done by bufferNext().
  ///\param tid The trackid of the page to start buffering
  ///\param pageNumber The number of the page to start buffering
  bool InOutBase::bufferStart(size_t idx, uint32_t pageNumber, IPC::sharedPage & page, DTSC::Meta & aMeta){
    VERYHIGH_MSG("bufferStart for stream %s, track %zu, page %" PRIu32, streamName.c_str(), idx, pageNumber);
    // Initialize the stream metadata if it does not yet exist
#ifndef TSLIVE_INPUT
    if (!aMeta){aMeta.reInit(streamName);}
#endif

    if (!aMeta.getValidTracks().size()){
      aMeta.clear();
      return false;
    }

    // If we are currently buffering a page, abandon it completely and print a message about this
    // This page will NEVER be deleted, unless we open it again later.
    if (page){
      WARN_MSG("Abandoning current page (%s) for track %zu", page.name.c_str(), idx);
      page.close();
    }

    Util::RelAccX &tPages = aMeta.pages(idx);

    uint32_t pageIdx = INVALID_KEY_NUM;
    for (uint32_t i = tPages.getDeleted(); i < tPages.getEndPos(); i++){
      if (tPages.getInt("firstkey", i) == pageNumber){
        pageIdx = i;
        break;
      }
    }

    // If this is not a valid page number on this track, stop buffering this page.
    if (pageIdx == INVALID_KEY_NUM){
      WARN_MSG("Aborting page buffer start: %" PRIu32 " is not a valid page number on track %zu.", pageNumber, idx);
      std::stringstream test;
      for (uint32_t i = tPages.getDeleted(); i < tPages.getEndPos(); i++){
        test << tPages.getInt("firstkey", i) << " ";
      }
      INFO_MSG("Valid page numbers: %s", test.str().c_str());
      ///\return false if the pagenumber is not valid for this track
      return false;
    }

    // If the page is already buffered, ignore this request
    if (isBuffered(idx, pageNumber, aMeta)){
      INFO_MSG("Page %" PRIu32 " on track %zu already buffered", pageNumber, idx);
      ///\return false if the page was already buffered.
      return false;
    }

    // Open the correct page for the data
    char pageId[NAME_BUFFER_SIZE];
    snprintf(pageId, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), idx, pageNumber);
    uint64_t pageSize = tPages.getInt("size", pageIdx);
    std::string pageName(pageId);
    page.init(pageName, pageSize, true);

    if (!page){
      ERROR_MSG("Could not open page %s", pageId);
      return false;
    }

    // Make sure the data page is not destroyed when we are done buffering it later on.
    page.master = false;

    // Set the current offset to 0, to allow for using it in bufferNext()
    tPages.setInt("avail", 0, pageIdx);

    HIGH_MSG("Start buffering page %" PRIu32 " on track %zu successful", pageNumber, idx);
    return true;
  }

  /// Checks whether a given page is currently being written to
  /// \return True if the page is the current live page, and thus not safe to remove
  bool InOutBase::isCurrentLivePage(size_t idx, uint32_t pageNumber){
    // Base case: for nonlive situations no new data will be added
    if (!M.getLive()){
      return false;
    }
    // All pages at or after the current live page should not get removed
    if (curPageNum[idx] && curPageNum[idx] <= pageNumber){
      return true;
    }
    // If there is no set curPageNum we are definitely not writing to it
    return false;
  }

  /// Removes a fully buffered page
  ///
  /// Does not do anything if the process is not standalone, in this case the master process will have an overloaded version of this function.
  ///\param tid The trackid to remove the page from
  ///\param pageNumber The number of the page to remove
  void InOutBase::bufferRemove(size_t idx, uint32_t pageNumber){
    if (!standAlone){// A different process will handle this for us
      return;
    }
    Util::RelAccX &tPages = meta.pages(idx);

    uint32_t pageIdx = INVALID_KEY_NUM;
    for (uint32_t i = tPages.getDeleted(); i < tPages.getEndPos(); i++){
      if (tPages.getInt("firstkey", i) == pageNumber){
        pageIdx = i;
        break;
      }
    }
    // If the given pagenumber is not a valid page on this track, do nothing
    if (pageIdx == INVALID_KEY_NUM){
      INFO_MSG("Can't remove page %" PRIu32 " on track %zu as it is not a valid page number.", pageNumber, idx);
      return;
    }

    HIGH_MSG("Removing page %" PRIu32 " on track %zu from the corresponding metaPage", pageNumber, idx);
    tPages.setInt("avail", 0, pageIdx);

    // Open the correct page
    char pageId[NAME_BUFFER_SIZE];
    snprintf(pageId, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), idx, pageNumber);
    std::string pageName(pageId);
    IPC::sharedPage toErase;
#ifdef __CYGWIN__
    toErase.init(pageName, 26 * 1024 * 1024, false, false);
#else
    toErase.init(pageName, tPages.getInt("size", pageIdx), false, false);
#endif
    // Set the master flag so that the page will be destroyed once it leaves scope
#if defined(__CYGWIN__) || defined(_WIN32)
    IPC::releasePage(pageName);
#endif
    toErase.master = true;
    // Remove the page from the tracks index page
    // Leaving scope here, the page will now be destroyed
  }

  /// Checks whether a key is buffered
  ///\param tid The trackid on which to locate the key
  ///\param keyNum The number of the keyframe to find
  bool InOutBase::isBuffered(size_t idx, uint32_t keyNum, DTSC::Meta & aMeta){
    ///\return The result of bufferedOnPage(tid, keyNum)
    return bufferedOnPage(idx, keyNum, aMeta) != INVALID_KEY_NUM;
  }

  /// Returns the pagenumber where this key is buffered on
  ///\param tid The trackid on which to locate the key
  ///\param keyNum The number of the keyframe to find
  uint32_t InOutBase::bufferedOnPage(size_t idx, uint32_t keyNum, DTSC::Meta & aMeta){
    Util::RelAccX &tPages = aMeta.pages(idx);

    for (uint64_t i = tPages.getDeleted(); i < tPages.getEndPos(); i++){
      uint64_t pageNum = tPages.getInt("firstkey", i);
      if (pageNum > keyNum) continue;
      uint64_t keyCount = tPages.getInt("keycount", i);
      if (pageNum + keyCount - 1 < keyNum) continue;
      if (keyCount && pageNum + keyCount - 1 < keyNum) continue;
      uint64_t avail = tPages.getInt("avail", i);
      return avail ? pageNum : INVALID_KEY_NUM;
    }
    return INVALID_KEY_NUM;
  }

  /// Buffers the next packet on the currently opened page
  ///\param pack The packet to buffer
  void InOutBase::bufferNext(uint64_t packTime, int64_t packOffset, uint32_t packTrack, const char *packData,
                             size_t packDataSize, uint64_t packBytePos, bool isKeyframe, IPC::sharedPage & page){
    bufferNext(packTime, packOffset, packTrack, packData, packDataSize, packBytePos, isKeyframe, page, meta);
  }

  /// Buffers the next packet on the currently opened page
  ///\param pack The packet to buffer
  void InOutBase::bufferNext(uint64_t packTime, int64_t packOffset, uint32_t packTrack, const char *packData,
                             size_t packDataSize, uint64_t packBytePos, bool isKeyframe, IPC::sharedPage & page, DTSC::Meta & aMeta){
    size_t packDataLen =
        24 + (packOffset ? 17 : 0) + (packBytePos ? 15 : 0) + (isKeyframe ? 19 : 0) + packDataSize + 11;

    static bool multiWrong = false;
    // Save the trackid of the track for easier access
    if (packTrack == INVALID_TRACK_ID){
      WARN_MSG("Packet with id %" PRIu32 " has an invalid track", packTrack);
      return;
    }

    // these checks were already done in bufferSinglePacket, but we check again just to be sure
    if (!aMeta.getVod() && packTime < aMeta.getLastms(packTrack)){
      DEBUG_MSG(((multiWrong == 0) ? DLVL_WARN : DLVL_HIGH),
                "Wrong order on track %" PRIu32 " ignored: %" PRIu64 " < %" PRIu64, packTrack,
                packTime, aMeta.getLastms(packTrack));
      multiWrong = true;
      return;
    }
    // Do nothing if no page is opened for this track
    if (!page){
      INFO_MSG("Trying to buffer a packet on track %" PRIu32 ", but no page is initialized", packTrack);
      return;
    }
    multiWrong = false;

    Util::RelAccX &tPages = aMeta.pages(packTrack);
    uint32_t pageIdx = 0;
    uint32_t currPagNum = atoi(page.name.data() + page.name.rfind('_') + 1);
    Util::RelAccXFieldData firstkey = tPages.getFieldData("firstkey");
    for (uint64_t i = tPages.getDeleted(); i < tPages.getEndPos(); i++){
      if (tPages.getInt(firstkey, i) == currPagNum){
        pageIdx = i;
        break;
      }
    }
    // Save the current write position
    uint64_t pageOffset = tPages.getInt("avail", pageIdx);
    uint64_t pageSize = tPages.getInt("size", pageIdx);
    INSANE_MSG("Current packet %" PRIu64 " on track %" PRIu32 " has an offset on page %s of %" PRIu64, packTime, packTrack, page.name.c_str(), pageOffset);
    // Do nothing when there is not enough free space on the page to add the packet.
    if (pageSize - pageOffset < packDataLen){
      FAIL_MSG("Track %" PRIu32 "p%" PRIu32 " : Pack %" PRIu64 "ms of %zub exceeds size %" PRIu64 " @ bpos %" PRIu64,
               packTrack, currPagNum, packTime, packDataLen, pageSize, pageOffset);
      return;
    }

    // First generate only the payload on the correct destination
    // Leaves the 20 bytes inbetween empty to ensure the data is not accidentally read before it is
    // complete
    char *data = page.mapped + pageOffset;

    data[20] = 0xE0; // start container object
    unsigned int offset = 21;
    if (packOffset){
      memcpy(data + offset, "\000\006offset\001", 9);
      Bit::htobll(data + offset + 9, packOffset);
      offset += 17;
    }
    if (packBytePos){
      memcpy(data + offset, "\000\004bpos\001", 7);
      Bit::htobll(data + offset + 7, packBytePos);
      offset += 15;
    }
    if (isKeyframe){
      memcpy(data + offset, "\000\010keyframe\001\000\000\000\000\000\000\000\001", 19);
      offset += 19;
    }
    memcpy(data + offset, "\000\004data\002", 7);
    Bit::htobl(data + offset + 7, packDataSize);
    memcpy(data + offset + 11, packData ? packData : 0, packDataSize);
    // finish container with 0x0000EE
    memcpy(data + offset + 11 + packDataSize, "\000\000\356", 3);

    // Copy the remaining values in reverse order:
    // 8 byte timestamp
    Bit::htobll(page.mapped + pageOffset + 12, packTime);
    // The mapped track id
    Bit::htobl(page.mapped + pageOffset + 8, packTrack);
    // Write the size
    Bit::htobl(page.mapped + pageOffset + 4, packDataLen - 8);
    // write the 'DTP2' bytes to conclude the packet and allow for reading it
    memcpy(page.mapped + pageOffset, "DTP2", 4);

    DONTEVEN_MSG("Setting page %" PRIu32 " available to %" PRIu64, pageIdx, pageOffset + packDataLen);
    tPages.setInt("avail", pageOffset + packDataLen, pageIdx);
  }

  /// Wraps up the buffering of a shared memory data page
  /// \param idx The track index of the page to finalize
  void InOutBase::liveFinalize(size_t idx){
    if (!livePage.count(idx)){return;}
    bufferFinalize(idx, livePage[idx]);
  }

  /// Wraps up the buffering of a shared memory data page
  /// \param idx The track index of the page to finalize
  void InOutBase::bufferFinalize(size_t idx, IPC::sharedPage & page){
    // If no page is open, do nothing
    if (!page){
      WARN_MSG("Trying to finalize the current page on track %zu, but no page is initialized", idx);
      return;
    }

/// \TODO META Re-Implement for Cygwin/Win32!
#if defined(__CYGWIN__) || defined(_WIN32)
    /*
    static int wipedAlready = 0;
    if (lowest && lowest > wipedAlready + 1){
      for (int curr = wipedAlready + 1; curr < lowest; ++curr){
        char pageId[NAME_BUFFER_SIZE];
        snprintf(pageId, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), idx, curr);
        IPC::releasePage(std::string(pageId));
      }
    }
    // Print a message about registering the page or not.
    if (inserted){IPC::preservePage(curPage[idx].name);}
    */
#endif
    // Close our link to the page. This will NOT destroy the shared page, as we've set master to
    // false upon construction Note: if there was a registering failure above, this WILL destroy the
    // shared page, to prevent a memory leak
    page.close();
  }

  /// Buffers a live packet to a page.
  /// Calls bufferLivePacket with full arguments internally.
  void InOutBase::bufferLivePacket(const DTSC::Packet &packet){
    size_t idx = M.trackIDToIndex(packet.getTrackId(), getpid());
    if (idx == INVALID_TRACK_ID){
      INFO_MSG("Packet for track %zu has no valid index!", packet.getTrackId());
      return;
    }
    char *data;
    size_t dataLen;
    packet.getString("data", data, dataLen);
    bufferLivePacket(packet.getTime(), packet.getInt("offset"), idx, data, dataLen,
                     packet.getInt("bpos"), packet.getFlag("keyframe"));
    /// \TODO META Build something that should actually be able to deal with "extra" values
  }

  /// Calls bufferLivePacket with additional argument for internal metadata reference internally.
  void InOutBase::bufferLivePacket(uint64_t packTime, int64_t packOffset, uint32_t packTrack, const char *packData,
                                   size_t packDataSize, uint64_t packBytePos, bool isKeyframe){
    bufferLivePacket(packTime, packOffset, packTrack, packData, packDataSize, packBytePos, isKeyframe, meta);
  }
  
  ///Buffers the given packet data into the given metadata structure.
  ///Uses class member variables livePage and curPageNum internally for bookkeeping.
  ///These member variables are not (and should not, in the future) be accessed anywhere else.
  void InOutBase::bufferLivePacket(uint64_t packTime, int64_t packOffset, uint32_t packTrack, const char *packData,
                                   size_t packDataSize, uint64_t packBytePos, bool isKeyframe, DTSC::Meta &aMeta){
    aMeta.reloadReplacedPagesIfNeeded();
    aMeta.setLive(true);

    // Store the trackid for easier access
    // Do nothing if the trackid is invalid
    if (packTrack == INVALID_TRACK_ID){return;}

    // Store the trackid for easier access
    Util::RelAccX &tPages = aMeta.pages(packTrack);

    if (aMeta.getType(packTrack) != "video"){
      isKeyframe = false;
      if (!tPages.getEndPos() || !livePage[packTrack]){
        // Assume this is the first packet on the track
        isKeyframe = true;
      }else{
        if (packTime - tPages.getInt("lastkeytime", tPages.getEndPos() - 1) >= AUDIO_KEY_INTERVAL){
          isKeyframe = true;
        }
      }
    }

    // For live streams, ignore packets that make no sense
    // This also happens in bufferNext, with the same rules
    if (aMeta.getLive()){
      if (packTime < aMeta.getLastms(packTrack)){
        HIGH_MSG("Wrong order on track %" PRIu32 " ignored: %" PRIu64 " < %" PRIu64, packTrack,
                 packTime, aMeta.getLastms(packTrack));
        return;
      }
      if (packTime > aMeta.getLastms(packTrack) + 30000 && aMeta.getLastms(packTrack)){
        WARN_MSG("Sudden jump in timestamp from %" PRIu64 " to %" PRIu64, aMeta.getLastms(packTrack), packTime);
      }
    }
    
    // Determine if we need to open the next page
    if (isKeyframe){
      updateTrackFromKeyframe(packTrack, packData, packDataSize, aMeta);
      uint64_t endPage = tPages.getEndPos();
      size_t curPage = 0;
      size_t currPagNum = atoi(livePage[packTrack].name.data() + livePage[packTrack].name.rfind('_') + 1);
      Util::RelAccXFieldData firstkey = tPages.getFieldData("firstkey");
      for (uint64_t i = tPages.getDeleted(); i < tPages.getEndPos(); i++){
        if (tPages.getInt(firstkey, i) == currPagNum){
          curPage = i;
          break;
        }
      }

      // If there is no page, create it
      if (!livePage[packTrack]){
        size_t keyNum = aMeta.getKeyNumForTime(packTrack, packTime);
        if (keyNum == INVALID_KEY_NUM){
          curPageNum[packTrack] = 0;
        }else{
          curPageNum[packTrack] = aMeta.getKeyNumForTime(packTrack, packTime) + 1;
        }

        if ((tPages.getEndPos() - tPages.getDeleted()) >= tPages.getRCount()){
          aMeta.resizeTrack(packTrack, aMeta.fragments(packTrack).getRCount(), aMeta.keys(packTrack).getRCount(), aMeta.parts(packTrack).getRCount(), tPages.getRCount() * 2, "not enough pages");
        }

        curPage = endPage;
        tPages.setInt("firstkey", curPageNum[packTrack], endPage);
        tPages.setInt("firsttime", packTime, endPage);
        tPages.setInt("size", DEFAULT_DATA_PAGE_SIZE, endPage);
        tPages.setInt("keycount", 0, endPage);
        tPages.setInt("avail", 0, endPage);
        tPages.addRecords(1);
        DONTEVEN_MSG("Opening new page #%zu to track %" PRIu32, curPageNum[packTrack], packTrack);
        if (!bufferStart(packTrack, curPageNum[packTrack], livePage[packTrack], aMeta)){
          // if this fails, return instantly without actually buffering the packet
          WARN_MSG("Dropping packet %s:%" PRIu32 "@%" PRIu64, streamName.c_str(), packTrack, packTime);
          return;
        }
      }else{
        uint64_t prevPageTime = tPages.getInt("firsttime", curPage);
        // Compare on 8 mb boundary and target duration
        if (tPages.getInt("avail", curPage) > FLIP_DATA_PAGE_SIZE || packTime - prevPageTime > FLIP_TARGET_DURATION){
          // Create the book keeping data for the new page
          curPageNum[packTrack] = tPages.getInt("firstkey", curPage) + tPages.getInt("keycount", curPage);
          DONTEVEN_MSG("Live page transition from %" PRIu32 ":%" PRIu64 " to %" PRIu32 ":%zu", packTrack,
                  tPages.getInt("firstkey", curPage), packTrack, curPageNum[packTrack]);

          if ((tPages.getEndPos() - tPages.getDeleted()) >= tPages.getRCount()){
            aMeta.resizeTrack(packTrack, aMeta.fragments(packTrack).getRCount(), aMeta.keys(packTrack).getRCount(), aMeta.parts(packTrack).getRCount(), tPages.getRCount() * 2, "not enough pages");
          }

          curPage = endPage;
          tPages.setInt("firstkey", curPageNum[packTrack], endPage);
          tPages.setInt("firsttime", packTime, endPage);
          tPages.setInt("size", DEFAULT_DATA_PAGE_SIZE, endPage);
          tPages.setInt("keycount", 0, endPage);
          tPages.setInt("avail", 0, endPage);
          tPages.addRecords(1);
          if (livePage[packTrack]){bufferFinalize(packTrack, livePage[packTrack]);}
          DONTEVEN_MSG("Opening new page #%zu to track %" PRIu32, curPageNum[packTrack], packTrack);
          if (!bufferStart(packTrack, curPageNum[packTrack], livePage[packTrack], aMeta)){
            // if this fails, return instantly without actually buffering the packet
            WARN_MSG("Dropping packet %s:%" PRIu32 "@%" PRIu64, streamName.c_str(), packTrack, packTime);
            return;
          }
        }
      }
      DONTEVEN_MSG("Setting page %" PRIu64 " lastkeyTime to %" PRIu64 " and keycount to %" PRIu64, tPages.getInt("firstkey", curPage), packTime, tPages.getInt("keycount", curPage) + 1);
      tPages.setInt("lastkeytime", packTime, curPage);
      tPages.setInt("keycount", tPages.getInt("keycount", curPage) + 1, curPage);
    }
    if (!livePage[packTrack]) {
      INFO_MSG("Track %" PRIu32 " page %zu not starting with a keyframe!", packTrack, curPageNum[packTrack]);
      return;
    }

    if (!livePage[packTrack].exists()){
      WARN_MSG("Data page '%s' was deleted - forcing source shutdown to prevent unstable state", livePage[packTrack].name.c_str());
      Util::logExitReason(ER_SHM_LOST, "data page was deleted, forcing shutdown to prevent unstable state");
      bufferFinalize(packTrack, livePage[packTrack]);
      kill(getpid(), SIGINT);
      return;
    }

    // Buffer the packet
    DONTEVEN_MSG("Buffering live packet (%zuB) @%" PRIu64 " ms on track %" PRIu32 " with offset %" PRIu64, packDataSize, packTime, packTrack, packOffset);
    bufferNext(packTime, packOffset, packTrack, packData, packDataSize, packBytePos, isKeyframe, livePage[packTrack], aMeta);
    aMeta.update(packTime, packOffset, packTrack, packDataSize, packBytePos, isKeyframe);
  }

  ///Handles updating track metadata from a new keyframe, if applicable
  void InOutBase::updateTrackFromKeyframe(uint32_t packTrack, const char *packData, size_t packDataSize, DTSC::Meta & aMeta){
    if (aMeta.getCodec(packTrack) == "H264"){
      //H264 packets are 4-byte size-prepended NAL units
      size_t offset = 0;
      while (offset+4 < packDataSize){
        uint32_t nalLen = Bit::btohl(packData+offset);
        if (nalLen+offset+4 > packDataSize){
          FAIL_MSG("Corrupt H264 keyframe packet: NAL unit of size %" PRIu32 " at position %zu exceeds packet size of %zu", nalLen, offset, packDataSize);
          return;
        }
        uint8_t nalType = (packData[offset+4] & 0x1F);
        if (nalType == 7){//SPS, update width/height/FPS
          h264::SPSMeta hMeta =  h264::sequenceParameterSet(packData+offset+4, nalLen).getCharacteristics();
          aMeta.setWidth(packTrack, hMeta.width);
          aMeta.setHeight(packTrack, hMeta.height);
          aMeta.setFpks(packTrack, hMeta.fps*1000);
        }
        offset += nalLen+4;
      }
    }
    if (aMeta.getCodec(packTrack) == "VP8"){
      //VP8 packets have a simple header for keyframes
      //Reference: https://www.rfc-editor.org/rfc/rfc6386.html#section-9.1
      if (packData[3] == 0x9d && packData[4] == 0x01 && packData[5] == 0x2a){
        //Probably a valid key frame
        uint16_t pixWidth = Bit::btohs_le(packData+6);
        uint16_t pixHeight = Bit::btohs_le(packData+8);
        uint32_t w = pixWidth & 0x3fff;
        uint32_t h = pixHeight & 0x3fff;
        switch (pixWidth >> 14){
          case 1: w *= 5/4; break;
          case 2: w *= 5/3; break;
          case 3: w *= 2; break;
        }
        switch (pixHeight >> 14){
          case 1: h *= 5/4; break;
          case 2: h *= 5/3; break;
          case 3: h *= 2; break;
        }
        aMeta.setWidth(packTrack, w);
        aMeta.setHeight(packTrack, h);
      }
    }
  }



}// namespace Mist
