#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>
#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/flv_tag.h>
#include <mist/h264.h>
#include <mist/stream.h>
#include <string>

#include "input_mp4.h"

namespace Mist{

  MP4::TrackHeader &InputMP4::headerData(size_t trackID){
    static MP4::TrackHeader none;
    for (std::deque<MP4::TrackHeader>::iterator it = trackHeaders.begin(); it != trackHeaders.end(); it++){
      if (it->trackId == trackID){return *it;}
    }
    return none;
  }

  InputMP4::InputMP4(Util::Config *cfg) : Input(cfg){
    capa["name"] = "MP4";
    capa["desc"] = "This input allows streaming of MP4 or MOV files as Video on Demand.";
    capa["source_match"].append("/*.mp4");
    capa["source_match"].append("http://*.mp4");
    capa["source_match"].append("https://*.mp4");
    capa["source_match"].append("s3+http://*.mp4");
    capa["source_match"].append("s3+https://*.mp4");
    capa["source_match"].append("/*.mov");
    capa["source_match"].append("http://*.mov");
    capa["source_match"].append("https://*.mov");
    capa["source_match"].append("s3+http://*.mov");
    capa["source_match"].append("s3+https://*.mov");
    capa["source_prefill"].append("/");
    capa["source_prefill"].append("http://");
    capa["source_prefill"].append("https://");
    capa["source_prefill"].append("s3+http://");
    capa["source_prefill"].append("s3+https://");
#if defined(__CYGWIN__)
    capa["source_syntax"].append("/cygdrive/[DRIVE/path/to/][file_name]");
#else
    capa["source_syntax"].append("/[path/to/][file_name]");
#endif
    capa["source_syntax"].append("http://[address]");
    capa["source_syntax"].append("https://[address]");
    capa["source_syntax"].append("s3+http://[address]");
    capa["source_syntax"].append("s3+https://[address]");
    capa["source_help"] = "Location where MistServer can find the input file.";
    capa["source_file"] = "$source";
    capa["priority"] = 9;
    capa["codecs"]["video"].append("HEVC");
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["video"].append("H263");
    capa["codecs"]["video"].append("VP6");
    capa["codecs"]["video"].append("AV1");
    capa["codecs"]["audio"].append("AAC");
    capa["codecs"]["audio"].append("AC3");
    capa["codecs"]["audio"].append("MP3");
    readPos = 0;
    nextBox = 0;
  }

  bool InputMP4::checkArguments(){
    if (config->getString("input") == "-"){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Input from stdin not yet supported");
      return false;
    }
    if (!config->getString("streamname").size()){
      if (config->getString("output") == "-"){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Output to stdout not yet supported");
        return false;
      }
    }else{
      if (config->getString("output") != "-"){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "File output in player mode not supported");
        return false;
      }
      streamName = config->getString("streamname");
    }
    return true;
  }

  bool InputMP4::preRun(){
    // open File
    inFile.open(config->getString("input"));
    if (!inFile){
      Util::logExitReason(ER_READ_START_FAILURE, "Could not open URL or contains no data");
      return false;
    }
    if (!inFile.isSeekable()){
      Util::logExitReason(ER_READ_START_FAILURE, "MP4 input only supports seekable data sources, for now, and this source is not seekable: %s", config->getString("input").c_str());
      return false;
    }
    return true;
  }

  void InputMP4::dataCallback(const char *ptr, size_t size){readBuffer.append(ptr, size);}
  size_t InputMP4::getDataCallbackPos() const{return readPos + readBuffer.size();}

  bool InputMP4::needHeader(){
    //Attempt to read cache, but force calling of the readHeader function anyway
    bool r = Input::needHeader();
    if (!r){r = !readHeader();}
    return r;
  }

  bool InputMP4::readHeader(){
    if (!inFile){
      Util::logExitReason(ER_READ_START_FAILURE, "Reading header for '%s' failed: Could not open input stream", config->getString("input").c_str());
      return false;
    }
    bool hasMoov = false;
    readBuffer.truncate(0);
    readPos = 0;

    // first we get the necessary header parts
    size_t tNumber = 0;
    activityCounter = Util::bootSecs();
    while ((readBuffer.size() >= 16 || inFile) && keepRunning()){
      //Read box header if needed
      while (readBuffer.size() < 16 && inFile && keepRunning()){inFile.readSome(16, *this);}
      //Failed? Abort.
      if (readBuffer.size() < 16){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Could not read box header from input!");
        break;
      }
      //Box type is always on bytes 5-8 from the start of a box
      std::string boxType = std::string(readBuffer+4, 4);
      uint64_t boxSize = MP4::calcBoxSize(readBuffer);
      if (boxType == "moov"){
        moovPos = readPos;
        while (readBuffer.size() < boxSize && inFile && keepRunning()){inFile.readSome(boxSize-readBuffer.size(), *this);}
        if (readBuffer.size() < boxSize){
          Util::logExitReason(ER_FORMAT_SPECIFIC, "Could not read entire MOOV box into memory");
          break;
        }
        MP4::Box moovBox(readBuffer, false);

        // for all box in moov
        std::deque<MP4::TRAK> trak = ((MP4::MOOV*)&moovBox)->getChildren<MP4::TRAK>();
        for (std::deque<MP4::TRAK>::iterator trakIt = trak.begin(); trakIt != trak.end(); trakIt++){
          trackHeaders.push_back(MP4::TrackHeader());
          trackHeaders.rbegin()->read(*trakIt);
        }
        std::deque<MP4::TREX> trex = ((MP4::MOOV*)&moovBox)->getChild<MP4::MVEX>().getChildren<MP4::TREX>();
        for (std::deque<MP4::TREX>::iterator trexIt = trex.begin(); trexIt != trex.end(); trexIt++){
          for (std::deque<MP4::TrackHeader>::iterator it = trackHeaders.begin(); it != trackHeaders.end(); it++){
            if (it->trackId == trexIt->getTrackID()){it->read(*trexIt);}
          }
        }
        hasMoov = true;
      }
      activityCounter = Util::bootSecs();
      //Skip to next box
      if (readBuffer.size() > boxSize){
        readBuffer.shift(boxSize);
        readPos += boxSize;
      }else{
        readBuffer.truncate(0);
        if (!inFile.seek(readPos + boxSize)){
          FAIL_MSG("Seek to %" PRIu64 " failed! Aborting load", readPos+boxSize);
        }
        readPos = inFile.getPos();
      }
      // Stop if we've found a MOOV box - we'll do the rest afterwards
      if (hasMoov){break;}
    }

    if (!hasMoov){
      if (!inFile){
        Util::logExitReason(ER_READ_START_FAILURE, "Reading header for '%s' failed: URIReader for source file was disconnected!", config->getString("input").c_str());
      }else{
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Reading header for '%s' failed: No MOOV box found in source file; aborting!", config->getString("input").c_str());
      }
      return false;
    }


    // If we already read a cached header, we can exit here.
    if (M){
      bps = 0;
      std::set<size_t> tracks = M.getValidTracks();
      for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){bps += M.getBps(*it);}
      return true;
    }

    meta.reInit(isSingular() ? streamName : "");
    tNumber = 0;
    bps = 0;

    bool sawParts = false;
    bool parsedInitial = false;
    for (std::deque<MP4::TrackHeader>::iterator it = trackHeaders.begin(); it != trackHeaders.end(); it++){
      if (!it->compatible()){
        INFO_MSG("Unsupported track: %s", it->trackType.c_str());
        continue;
      }
      tNumber = meta.addTrack();
      HIGH_MSG("Found track %zu of type %s -> %s", tNumber, it->sType.c_str(), it->codec.c_str());
      meta.setID(tNumber, it->trackId);
      meta.setCodec(tNumber, it->codec);
      meta.setInit(tNumber, it->initData);
      meta.setLang(tNumber, it->lang);
      if (it->trackType == "video"){
        meta.setType(tNumber, "video");
        meta.setWidth(tNumber, it->vidWidth);
        meta.setHeight(tNumber, it->vidHeight);
      }
      if (it->trackType == "audio"){
        meta.setType(tNumber, "audio");
        meta.setChannels(tNumber, it->audChannels);
        meta.setRate(tNumber, it->audRate);
        meta.setSize(tNumber, it->audSize);
      }
      if (it->size()){sawParts = true;}
    }

    // Might be an fMP4 file! Let's try finding some moof boxes...
    bool sawMoof = false;
    size_t moofPos = 0;
    while ((readBuffer.size() >= 16 || inFile) && keepRunning()){
      //Read box header if needed
      while (readBuffer.size() < 16 && inFile && keepRunning()){inFile.readSome(16, *this);}
      //Failed? Abort - this is not fatal, unlike in the loop above (could just be EOF).
      if (readBuffer.size() < 16){break;}
      //Box type is always on bytes 5-8 from the start of a box
      std::string boxType = std::string(readBuffer+4, 4);
      uint64_t boxSize = MP4::calcBoxSize(readBuffer);
      if (boxType == "moof"){
        while (readBuffer.size() < boxSize && inFile && keepRunning()){inFile.readSome(boxSize-readBuffer.size(), *this);}
        if (readBuffer.size() < boxSize){
          WARN_MSG("Could not read entire MOOF box into memory at %" PRIu64 " bytes, aborting further parsing!", readPos);
          break;
        }
        if (sawParts && !parsedInitial){
          for (std::deque<MP4::TrackHeader>::iterator it = trackHeaders.begin(); it != trackHeaders.end(); ++it){
            tNumber = M.trackIDToIndex(it->trackId);
            for (uint64_t partNo = 0; partNo < it->size(); ++partNo){
              uint64_t prtBpos = 0, prtTime = 0;
              uint32_t prtBlen = 0;
              int32_t prtTimeOff = 0;
              bool prtKey = false;
              it->getPart(partNo, &prtBpos, &prtBlen, &prtTime, &prtTimeOff, &prtKey);
              meta.update(prtTime, prtTimeOff, tNumber, prtBlen, moovPos, prtKey && it->trackType != "audio");
              sawParts = true;
            }
            bps += M.getBps(tNumber);
          }
          parsedInitial = true;
        }
        MP4::Box moofBox(readBuffer, false);
        moofPos = readPos;
        // Indicate that we're reading the next moof box to all track headers
        for (std::deque<MP4::TrackHeader>::iterator t = trackHeaders.begin(); t != trackHeaders.end(); ++t){
          t->nextMoof();
        }
        // Loop over traf boxes inside the moof box, but them in our header parser
        std::deque<MP4::TRAF> trafs = ((MP4::MOOF*)&moofBox)->getChildren<MP4::TRAF>();
        for (std::deque<MP4::TRAF>::iterator t = trafs.begin(); t != trafs.end(); ++t){
          if (!(t->getChild<MP4::TFHD>())){
            WARN_MSG("Could not find thfd box inside traf box!");
            continue;
          }
          uint32_t trackId = t->getChild<MP4::TFHD>().getTrackID();
          headerData(trackId).read(*t);
        }

        // Parse data from moof header into our header
        for (std::deque<MP4::TrackHeader>::iterator it = trackHeaders.begin(); it != trackHeaders.end(); it++){
          if (!it->compatible()){continue;}
          tNumber = M.trackIDToIndex(it->trackId);
          for (uint64_t partNo = 0; partNo < it->size(); ++partNo){
            uint64_t prtBpos = 0, prtTime = 0;
            uint32_t prtBlen = 0;
            int32_t prtTimeOff = 0;
            bool prtKey = false;
            it->getPart(partNo, &prtBpos, &prtBlen, &prtTime, &prtTimeOff, &prtKey, moofPos);
            // Skip any parts that are outside the file limits
            if (inFile.getSize() != std::string::npos && prtBpos + prtBlen > inFile.getSize()){continue;}
            // Note: we set the byte position to the position of the moof, so we can re-read it later with ease
            meta.update(prtTime, prtTimeOff, tNumber, prtBlen, moofPos, prtKey && it->trackType != "audio");
            sawParts = true;
          }
        }
      }
      activityCounter = Util::bootSecs();
      //Skip to next box
      if (readBuffer.size() > boxSize){
        readBuffer.shift(boxSize);
        readPos += boxSize;
      }else{
        readBuffer.truncate(0);
        if (!inFile.seek(readPos + boxSize)){
          FAIL_MSG("Seek to %" PRIu64 " failed! Aborting load", readPos+boxSize);
        }
        readPos = inFile.getPos();
      }
    }
    if (!sawParts){
      WARN_MSG("Could not find any MOOF boxes with data, either! Considering load failed and aborting.");
      return false;
    }
    // Mark file as fmp4 so we know to treat it as one
    if (sawMoof){meta.inputLocalVars["fmp4"] = true;}
    if (!parsedInitial){
      for (std::deque<MP4::TrackHeader>::iterator it = trackHeaders.begin(); it != trackHeaders.end(); ++it){
        tNumber = M.trackIDToIndex(it->trackId);
        for (uint64_t partNo = 0; partNo < it->size(); ++partNo){
          uint64_t prtBpos = 0, prtTime = 0;
          uint32_t prtBlen = 0;
          int32_t prtTimeOff = 0;
          bool prtKey = false;
          it->getPart(partNo, &prtBpos, &prtBlen, &prtTime, &prtTimeOff, &prtKey);
          meta.update(prtTime, prtTimeOff, tNumber, prtBlen, prtBpos, prtKey && it->trackType != "audio");
          sawParts = true;
        }
        bps += M.getBps(tNumber);
      }
    }

    return true;
  }

  void InputMP4::getNext(size_t idx){// get next part from track in stream
    thisPacket.null();

    if (curPositions.empty()){
      // fMP4 file? Seek to the right header and read it in
      if (nextBox){
        uint32_t trackId = M.getID(idx);
        MP4::TrackHeader & thisHeader = headerData(trackId);

        std::string boxType;
        while (boxType != "moof"){
          if (!shiftTo(nextBox, 12)){return;}
          boxType = std::string(readBuffer+(nextBox - readPos)+4, 4);
          if (boxType == "moof"){break;}
          if (boxType != "mdat"){INFO_MSG("Skipping box: %s", boxType.c_str());}
          uint64_t boxSize = MP4::calcBoxSize(readBuffer + (nextBox - readPos));
          nextBox += boxSize;
        }

        uint64_t boxSize = MP4::calcBoxSize(readBuffer + (nextBox - readPos));
        if (!shiftTo(nextBox, boxSize)){return;}
        uint64_t moofPos = nextBox;

        {
          MP4::Box moofBox(readBuffer + (nextBox-readPos), false);

          thisHeader.nextMoof();
          // Loop over traf boxes inside the moof box, but them in our header parser
          std::deque<MP4::TRAF> trafs = ((MP4::MOOF*)&moofBox)->getChildren<MP4::TRAF>();
          for (std::deque<MP4::TRAF>::iterator t = trafs.begin(); t != trafs.end(); ++t){
            if (!(t->getChild<MP4::TFHD>())){
              WARN_MSG("Could not find thfd box inside traf box!");
              continue;
            }
            if (t->getChild<MP4::TFHD>().getTrackID() == trackId){thisHeader.read(*t);}
          }
        }

        size_t headerDataSize = thisHeader.size();
        MP4::PartTime addPart;
        addPart.trackID = idx;
        for (size_t i = 0; i < headerDataSize; i++){
          thisHeader.getPart(i, &addPart.bpos, &addPart.size, &addPart.time, &addPart.offset, &addPart.keyframe, moofPos);
          addPart.index = i;
          curPositions.insert(addPart);
        }
        nextBox += boxSize;
      }
    }

    // pop uit set
    MP4::PartTime curPart = *curPositions.begin();
    curPositions.erase(curPositions.begin());

    if (!shiftTo(curPart.bpos, curPart.size)){return;}

    if (M.getCodec(curPart.trackID) == "subtitle"){
      static JSON::Value thisPack;
      thisPack["trackid"] = (uint64_t)curPart.trackID;
      thisPack["bpos"] = curPart.bpos; //(long long)fileSource.tellg();
      thisPack["data"] = std::string(readBuffer + (curPart.bpos-readPos), curPart.size);
      thisPack["time"] = curPart.time;
      if (curPart.duration){thisPack["duration"] = curPart.duration;}
      thisPack["keyframe"] = true;
      std::string tmpStr = thisPack.toNetPacked();
      thisPacket.reInit(tmpStr.data(), tmpStr.size());
    }else{
      bool isKeyframe = (curPart.keyframe && meta.getType(curPart.trackID) == "video");
      thisPacket.genericFill(curPart.time, curPart.offset, curPart.trackID, readBuffer + (curPart.bpos-readPos), curPart.size, 0, isKeyframe);
    }
    thisTime = curPart.time;
    thisIdx = curPart.trackID;

    if (!nextBox){
      // get the next part for this track
      curPart.index++;
      if (curPart.index < headerData(M.getID(curPart.trackID)).size()){
        headerData(M.getID(curPart.trackID)).getPart(curPart.index, &curPart.bpos, &curPart.size, &curPart.time, &curPart.offset, &curPart.keyframe);
        curPositions.insert(curPart);
      }
    }
  }

  void InputMP4::seek(uint64_t seekTime, size_t idx){// seek to a point
    curPositions.clear();
    if (idx == INVALID_TRACK_ID){
      FAIL_MSG("Seeking more than 1 track at a time in MP4 input is unsupported");
      return;
    }

    MP4::PartTime addPart;
    addPart.trackID = idx;
    size_t trackId = M.getID(idx);
    MP4::TrackHeader &thisHeader = headerData(M.getID(idx));
    uint64_t moofPos = 0;

    // fMP4 file? Seek to the right header and read it in
    if (M.inputLocalVars.isMember("fmp4")){
      uint32_t keyIdx = M.getKeyIndexForTime(idx, seekTime);
      size_t bPos = M.getKeys(idx).getBpos(keyIdx);
      if (bPos == moovPos){
        thisHeader.revertToMoov();
        if (!shiftTo(bPos, 12)){return;}
        uint64_t boxSize = MP4::calcBoxSize(readBuffer + (bPos - readPos));
        nextBox = bPos + boxSize;
      }else{
        if (!shiftTo(bPos, 12)){return;}
        std::string boxType = std::string(readBuffer+(bPos - readPos)+4, 4);
        if (boxType != "moof"){
          FAIL_MSG("Read %s box instead of moof box at %zub!", boxType.c_str(), bPos);
          Util::logExitReason(ER_FORMAT_SPECIFIC, "Did not find moof box at expected position");
          return;
        }
        uint64_t boxSize = MP4::calcBoxSize(readBuffer + (bPos - readPos));
        if (!shiftTo(bPos, boxSize)){return;}
        
        MP4::Box moofBox(readBuffer + (bPos-readPos), false);
        moofPos = bPos;

        thisHeader.nextMoof();
        // Loop over traf boxes inside the moof box, put them in our header parser
        std::deque<MP4::TRAF> trafs = ((MP4::MOOF*)&moofBox)->getChildren<MP4::TRAF>();
        for (std::deque<MP4::TRAF>::iterator t = trafs.begin(); t != trafs.end(); ++t){
          if (!(t->getChild<MP4::TFHD>())){
            WARN_MSG("Could not find thfd box inside traf box!");
            continue;
          }
          if (t->getChild<MP4::TFHD>().getTrackID() == trackId){thisHeader.read(*t);}
        }
        nextBox = bPos + boxSize;
      }
    }

    size_t headerDataSize = thisHeader.size();
    for (size_t i = 0; i < headerDataSize; i++){
      thisHeader.getPart(i, &addPart.bpos, &addPart.size, &addPart.time, &addPart.offset, &addPart.keyframe, moofPos);

      // Skip any parts that are outside the file limits
      if (inFile.getSize() != std::string::npos && addPart.bpos + addPart.size > inFile.getSize()){continue;}

      if (addPart.time >= seekTime){
        addPart.index = i;
        curPositions.insert(addPart);
        if (!nextBox){break;}
      }
    }
  }

  /// Shifts the read buffer (if needed) so that bytes pos through pos+len are currently buffered.
  /// Returns true on success, false on failure.
  bool InputMP4::shiftTo(size_t pos, size_t len){
    if (pos < readPos || pos > readPos + readBuffer.size() + 512*1024 + bps){
      INFO_MSG("Buffer contains %" PRIu64 "-%" PRIu64 ", but we need %zu; seeking!", readPos, readPos + readBuffer.size(), pos);
      readBuffer.truncate(0);
      if (!inFile.seek(pos)){
        return false;
      }
      readPos = inFile.getPos();
    }else{
      //If we have more than 5MiB buffered and are more than 5MiB into the buffer, shift the first 4MiB off the buffer.
      //This prevents infinite growth of the read buffer for large files
      if (readBuffer.size() >= 5*1024*1024 && pos > readPos + 5*1024*1024 + bps){
        readBuffer.shift(4*1024*1024);
        readPos += 4*1024*1024;
      }
    }

    while (readPos+readBuffer.size() < pos+len && inFile && keepRunning()){
      inFile.readSome((pos+len) - (readPos+readBuffer.size()), *this);
    }
    if (readPos+readBuffer.size() < pos+len){
      if (inFile.getSize() != std::string::npos || inFile.getSize() > readPos+readBuffer.size()){
        FAIL_MSG("Read unsuccessful at %" PRIu64 ", seeking to retry...", readPos+readBuffer.size());
        readBuffer.truncate(0);
        if (!inFile.seek(pos)){
          return false;
        }
        readPos = inFile.getPos();
        while (readPos+readBuffer.size() < pos+len && inFile && keepRunning()){
          inFile.readSome((pos+len) - (readPos+readBuffer.size()), *this);
        }
        if (readPos+readBuffer.size() < pos+len){
          return false;
        }
      }else{
        WARN_MSG("Attempt to read past end of file!");
        return false;
      }
    }
    return true;
  }

}// namespace Mist
