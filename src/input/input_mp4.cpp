#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <inttypes.h>
#include <iostream>
#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/flv_tag.h>
#include <mist/h264.h>
#include <mist/stream.h>
#include <string>

#include "input_mp4.h"

namespace Mist{

  mp4TrackHeader::mp4TrackHeader(){
    initialised = false;
    stscStart = 0;
    sampleIndex = 0;
    deltaIndex = 0;
    deltaPos = 0;
    deltaTotal = 0;
    offsetIndex = 0;
    offsetPos = 0;
    stscBox.clear();
    stszBox.clear();
    stcoBox.clear();
    co64Box.clear();
    stco64 = false;
    trackId = 0;
  }

  uint64_t mp4TrackHeader::size(){return (stszBox.asBox() ? stszBox.getSampleCount() : 0);}

  void mp4TrackHeader::read(MP4::TRAK &trakBox){
    initialised = false;
    std::string tmp; // temporary string for copying box data
    MP4::Box trakLoopPeek;
    timeScale = 1;

    MP4::MDIA mdiaBox = trakBox.getChild<MP4::MDIA>();

    timeScale = mdiaBox.getChild<MP4::MDHD>().getTimeScale();
    trackId = trakBox.getChild<MP4::TKHD>().getTrackID();

    MP4::STBL stblBox = mdiaBox.getChild<MP4::MINF>().getChild<MP4::STBL>();

    stszBox.copyFrom(stblBox.getChild<MP4::STSZ>());
    stcoBox.copyFrom(stblBox.getChild<MP4::STCO>());
    co64Box.copyFrom(stblBox.getChild<MP4::CO64>());
    stscBox.copyFrom(stblBox.getChild<MP4::STSC>());
    stco64 = co64Box.isType("co64");
  }

  void mp4TrackHeader::getPart(uint64_t index, uint64_t &offset){
    if (index < sampleIndex){
      sampleIndex = 0;
      stscStart = 0;
    }

    uint64_t stscCount = stscBox.getEntryCount();
    MP4::STSCEntry stscEntry;
    while (stscStart < stscCount){
      stscEntry = stscBox.getSTSCEntry(stscStart);
      // check where the next index starts
      uint64_t nextSampleIndex;
      if (stscStart + 1 < stscCount){
        nextSampleIndex = sampleIndex + (stscBox.getSTSCEntry(stscStart + 1).firstChunk - stscEntry.firstChunk) *
                                            stscEntry.samplesPerChunk;
      }else{
        nextSampleIndex = stszBox.getSampleCount();
      }
      if (nextSampleIndex > index){break;}
      sampleIndex = nextSampleIndex;
      ++stscStart;
    }

    if (sampleIndex > index){
      FAIL_MSG("Could not complete seek - not in file (%" PRIu64 " > %" PRIu64 ")", sampleIndex, index);
    }

    uint64_t stcoPlace = (stscEntry.firstChunk - 1) + ((index - sampleIndex) / stscEntry.samplesPerChunk);
    uint64_t stszStart = sampleIndex + (stcoPlace - (stscEntry.firstChunk - 1)) * stscEntry.samplesPerChunk;

    offset = (stco64 ? co64Box.getChunkOffset(stcoPlace) : stcoBox.getChunkOffset(stcoPlace));
    for (int j = stszStart; j < index; j++){offset += stszBox.getEntrySize(j);}

    initialised = true;
  }

  mp4TrackHeader &inputMP4::headerData(size_t trackID){
    static mp4TrackHeader none;
    for (std::deque<mp4TrackHeader>::iterator it = trackHeaders.begin(); it != trackHeaders.end(); it++){
      if (it->trackId == trackID){return *it;}
    }
    return none;
  }

  inputMP4::inputMP4(Util::Config *cfg) : Input(cfg){
    capa["name"] = "MP4";
    capa["desc"] = "This input allows streaming of MP4 or MOV files as Video on Demand.";
    capa["source_match"].append("/*.mp4");
    capa["source_match"].append("http://*.mp4");
    capa["source_match"].append("https://*.mp4");
    capa["source_match"].append("s3+http://*.mp4");
    capa["source_match"].append("s3+https://*.mp4");
    capa["source_match"].append("mp4:*");
    capa["source_match"].append("/*.mov");
    capa["source_match"].append("http://*.mov");
    capa["source_match"].append("https://*.mov");
    capa["source_match"].append("s3+http://*.mov");
    capa["source_match"].append("s3+https://*.mov");
    capa["source_match"].append("mov:*");
    capa["source_file"] = "$source";
    capa["priority"] = 9;
    capa["codecs"]["video"].append("HEVC");
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["video"].append("H263");
    capa["codecs"]["video"].append("VP6");
    capa["codecs"]["audio"].append("AAC");
    capa["codecs"]["audio"].append("AC3");
    capa["codecs"]["audio"].append("MP3");
    readPos = 0;
  }

  bool inputMP4::checkArguments(){
    std::string inUrl = config->getString("input");
    if (inUrl.size() > 4 && inUrl.substr(0, 4) == "mp4:"){
      inUrl.erase(0, 4);
      config->getOption("input", true).append(inUrl);
    }
    if (config->getString("input") == "-"){
      std::cerr << "Input from stdin not yet supported" << std::endl;
      return false;
    }
    if (!config->getString("streamname").size()){
      if (config->getString("output") == "-"){
        std::cerr << "Output to stdout not yet supported" << std::endl;
        return false;
      }
    }else{
      if (config->getString("output") != "-"){
        std::cerr << "File output in player mode not supported" << std::endl;
        return false;
      }
      streamName = config->getString("streamname");
    }
    return true;
  }

  bool inputMP4::preRun(){
    // open File
    std::string inUrl = config->getString("input");
    inFile.open(inUrl);
    if (!inFile){
      Util::logExitReason(ER_READ_START_FAILURE, "Could not open URL or contains no data");
      return false;
    }
    if (!inFile.isSeekable()){
      FAIL_MSG("MP4 input only supports seekable data sources, for now, and this source is not seekable: %s", config->getString("input").c_str());
      return false;
    }
    return true;
  }

  void inputMP4::dataCallback(const char *ptr, size_t size){readBuffer.append(ptr, size);}

  bool inputMP4::readHeader(){
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
        FAIL_MSG("Could not read box header from input!");
        break;
      }
      //Box type is always on bytes 5-8 from the start of a box
      std::string boxType = std::string(readBuffer+4, 4);
      uint64_t boxSize = MP4::calcBoxSize(readBuffer);
      if (boxType == "moov"){
        while (readBuffer.size() < boxSize && inFile && keepRunning()){inFile.readSome(boxSize-readBuffer.size(), *this);}
        if (readBuffer.size() < boxSize){
          FAIL_MSG("Could not read entire MOOV box into memory");
          break;
        }
        MP4::Box moovBox(readBuffer, false);

        // for all box in moov
        std::deque<MP4::TRAK> trak = ((MP4::MOOV*)&moovBox)->getChildren<MP4::TRAK>();
        for (std::deque<MP4::TRAK>::iterator trakIt = trak.begin(); trakIt != trak.end(); trakIt++){
          trackHeaders.push_back(mp4TrackHeader());
          trackHeaders.rbegin()->read(*trakIt);
        }
        hasMoov = true;
        break;
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

    if (!hasMoov){
      if (!inFile){Util::logExitReason(ER_READ_START_FAILURE, "URIReader for source file was disconnected!");}
      Util::logExitReason(ER_FORMAT_SPECIFIC, "No MOOV box found in source file; aborting!");
      return false;
    }


    // See whether a separate header file exists.
    if (readExistingHeader()){
      bps = 0;
      std::set<size_t> tracks = M.getValidTracks();
      for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){bps += M.getBps(*it);}
      return true;
    }
    INFO_MSG("Not reading existing header");

    meta.reInit(isSingular() ? streamName : "");
    tNumber = 0;
    // Create header file from MP4 data
    MP4::Box moovBox(readBuffer, false);

    std::deque<MP4::TRAK> trak = ((MP4::MOOV*)&moovBox)->getChildren<MP4::TRAK>();
    HIGH_MSG("Obtained %zu trak Boxes", trak.size());

    for (std::deque<MP4::TRAK>::iterator trakIt = trak.begin(); trakIt != trak.end(); trakIt++){
      MP4::MDIA mdiaBox = trakIt->getChild<MP4::MDIA>();

      std::string hdlrType = mdiaBox.getChild<MP4::HDLR>().getHandlerType();
      if (hdlrType != "vide" && hdlrType != "soun" && hdlrType != "sbtl"){
        INFO_MSG("Unsupported handler: %s", hdlrType.c_str());
        continue;
      }

      MP4::STBL stblBox = mdiaBox.getChild<MP4::MINF>().getChild<MP4::STBL>();

      MP4::STSD stsdBox = stblBox.getChild<MP4::STSD>();
      MP4::Box sEntryBox = stsdBox.getEntry(0);
      std::string sType = sEntryBox.getType();

      if (!(sType == "avc1" || sType == "h264" || sType == "mp4v" || sType == "hev1" || sType == "hvc1" || sType == "mp4a" || sType == "aac " || sType == "ac-3" || sType == "tx3g")){
        INFO_MSG("Unsupported track type: %s", sType.c_str());
        continue;
      }

      tNumber = meta.addTrack();

      MP4::TKHD tkhdBox = trakIt->getChild<MP4::TKHD>();
      if (tkhdBox.getWidth() > 0){
        meta.setWidth(tNumber, tkhdBox.getWidth());
        meta.setHeight(tNumber, tkhdBox.getHeight());
      }
      meta.setID(tNumber, tkhdBox.getTrackID());

      MP4::MDHD mdhdBox = mdiaBox.getChild<MP4::MDHD>();
      uint64_t timescale = mdhdBox.getTimeScale();
      meta.setLang(tNumber, mdhdBox.getLanguage());

      HIGH_MSG("Found track %zu of type %s", tNumber, sType.c_str());

      if (sType == "avc1" || sType == "h264" || sType == "mp4v"){
        MP4::VisualSampleEntry &vEntryBox = (MP4::VisualSampleEntry &)sEntryBox;
        meta.setType(tNumber, "video");
        meta.setCodec(tNumber, "H264");
        if (!meta.getWidth(tNumber)){
          meta.setWidth(tNumber, vEntryBox.getWidth());
          meta.setHeight(tNumber, vEntryBox.getHeight());
        }
        MP4::Box initBox = vEntryBox.getCLAP();
        if (initBox.isType("avcC")){
          meta.setInit(tNumber, initBox.payload(), initBox.payloadSize());
        }
        initBox = vEntryBox.getPASP();
        if (initBox.isType("avcC")){
          meta.setInit(tNumber, initBox.payload(), initBox.payloadSize());
        }
        /// this is a hacky way around invalid FLV data (since it gets ignored nearly
        /// everywhere, but we do need correct data...
        if (!meta.getWidth(tNumber)){
          h264::sequenceParameterSet sps;
          sps.fromDTSCInit(meta.getInit(tNumber));
          h264::SPSMeta spsChar = sps.getCharacteristics();
          meta.setWidth(tNumber, spsChar.width);
          meta.setHeight(tNumber, spsChar.height);
        }
      }
      if (sType == "hev1" || sType == "hvc1"){
        MP4::VisualSampleEntry &vEntryBox = (MP4::VisualSampleEntry &)sEntryBox;
        meta.setType(tNumber, "video");
        meta.setCodec(tNumber, "HEVC");
        if (!meta.getWidth(tNumber)){
          meta.setWidth(tNumber, vEntryBox.getWidth());
          meta.setHeight(tNumber, vEntryBox.getHeight());
        }
        MP4::Box initBox = vEntryBox.getCLAP();
        if (initBox.isType("hvcC")){
          meta.setInit(tNumber, initBox.payload(), initBox.payloadSize());
        }
        initBox = vEntryBox.getPASP();
        if (initBox.isType("hvcC")){
          meta.setInit(tNumber, initBox.payload(), initBox.payloadSize());
        }
      }
      if (sType == "mp4a" || sType == "aac " || sType == "ac-3"){
        MP4::AudioSampleEntry &aEntryBox = (MP4::AudioSampleEntry &)sEntryBox;
        meta.setType(tNumber, "audio");
        meta.setChannels(tNumber, aEntryBox.getChannelCount());
        meta.setRate(tNumber, aEntryBox.getSampleRate());

        if (sType == "ac-3"){
          meta.setCodec(tNumber, "AC3");
        }else{
          MP4::Box codingBox = aEntryBox.getCodecBox();
          if (codingBox.getType() == "esds"){
            MP4::ESDS & esdsBox = (MP4::ESDS &)codingBox;
            meta.setCodec(tNumber, esdsBox.getCodec());
            meta.setInit(tNumber, esdsBox.getInitData());
          }
          if (codingBox.getType() == "wave"){
            MP4::WAVE & waveBox = (MP4::WAVE &)codingBox;
            for (size_t c = 0; c < waveBox.getContentCount(); ++c){
              MP4::Box content = waveBox.getContent(c);
              if (content.getType() == "esds"){
                MP4::ESDS & esdsBox = (MP4::ESDS &)content;
                meta.setCodec(tNumber, esdsBox.getCodec());
                meta.setInit(tNumber, esdsBox.getInitData());
              }
            }
          }
        }
        meta.setSize(tNumber, 16); ///\todo this might be nice to calculate from mp4 file;
      }
      if (sType == "tx3g"){// plain text subtitles
        meta.setType(tNumber, "meta");
        meta.setCodec(tNumber, "subtitle");
      }

      MP4::STSS stssBox = stblBox.getChild<MP4::STSS>();
      MP4::STTS sttsBox = stblBox.getChild<MP4::STTS>();
      MP4::STSZ stszBox = stblBox.getChild<MP4::STSZ>();
      MP4::STCO stcoBox = stblBox.getChild<MP4::STCO>();
      MP4::CO64 co64Box = stblBox.getChild<MP4::CO64>();
      MP4::STSC stscBox = stblBox.getChild<MP4::STSC>();
      MP4::CTTS cttsBox = stblBox.getChild<MP4::CTTS>(); // optional ctts box

      bool stco64 = co64Box.isType("co64");
      bool hasCTTS = cttsBox.isType("ctts");

      uint64_t totaldur = 0; ///\todo note: set this to begin time
      uint64_t totalExtraDur = 0;
      mp4PartBpos BsetPart;

      uint64_t entryNo = 0;
      uint64_t sampleNo = 0;

      uint64_t stssIndex = 0;
      uint64_t stcoIndex = 0;
      uint64_t stscIndex = 0;
      uint64_t cttsIndex = 0;     // current ctts Index we are reading
      uint64_t cttsEntryRead = 0; // current part of ctts we are reading

      uint64_t stssCount = stssBox.getEntryCount();
      uint64_t stscCount = stscBox.getEntryCount();
      uint64_t stszCount = stszBox.getSampleCount();
      uint64_t stcoCount = (stco64 ? co64Box.getEntryCount() : stcoBox.getEntryCount());

      MP4::STTSEntry sttsEntry = sttsBox.getSTTSEntry(0);

      uint32_t fromSTCOinSTSC = 0;
      uint64_t tmpOffset = (stco64 ? co64Box.getChunkOffset(0) : stcoBox.getChunkOffset(0));

      uint64_t nextFirstChunk = (stscCount > 1 ? stscBox.getSTSCEntry(1).firstChunk - 1 : stcoCount);

      for (uint64_t stszIndex = 0; stszIndex < stszCount; ++stszIndex){
        if (stcoIndex >= nextFirstChunk){
          ++stscIndex;
          nextFirstChunk =
              (stscIndex + 1 < stscCount ? stscBox.getSTSCEntry(stscIndex + 1).firstChunk - 1 : stcoCount);
        }
        BsetPart.keyframe = (meta.getType(tNumber) == "video" && stssIndex < stssCount &&
                             stszIndex + 1 == stssBox.getSampleNumber(stssIndex));
        if (BsetPart.keyframe){++stssIndex;}
        // in bpos set
        BsetPart.stcoNr = stcoIndex;
        // bpos = chunkoffset[samplenr] in stco
        BsetPart.bpos = tmpOffset;
        ++fromSTCOinSTSC;
        if (fromSTCOinSTSC < stscBox.getSTSCEntry(stscIndex).samplesPerChunk){// as long as we are still in this chunk
          tmpOffset += stszBox.getEntrySize(stszIndex);
        }else{
          ++stcoIndex;
          fromSTCOinSTSC = 0;
          tmpOffset = (stco64 ? co64Box.getChunkOffset(stcoIndex) : stcoBox.getChunkOffset(stcoIndex));
        }
        BsetPart.time = (totaldur * 1000) / timescale;
        totaldur += sttsEntry.sampleDelta;
        
        //Undo time shifts as much as possible
        if (totalExtraDur){
          totaldur -= totalExtraDur;
          totalExtraDur = 0;
        }

        //Make sure our timestamps go up by at least 1 for every packet
        if (BsetPart.time >= (uint64_t)((totaldur * 1000) / timescale)){
          uint32_t wantSamples = ((BsetPart.time+1) * timescale) / 1000;
          totalExtraDur += wantSamples - totaldur;
          totaldur = wantSamples;
        }

        sampleNo++;
        if (sampleNo >= sttsEntry.sampleCount){
          ++entryNo;
          sampleNo = 0;
          if (entryNo < sttsBox.getEntryCount()){sttsEntry = sttsBox.getSTTSEntry(entryNo);}
        }

        if (hasCTTS){
          MP4::CTTSEntry cttsEntry = cttsBox.getCTTSEntry(cttsIndex);
          cttsEntryRead++;
          if (cttsEntryRead >= cttsEntry.sampleCount){
            ++cttsIndex;
            cttsEntryRead = 0;
          }
          BsetPart.timeOffset = (cttsEntry.sampleOffset * 1000) / timescale;
        }else{
          BsetPart.timeOffset = 0;
        }

        if (sType == "tx3g"){
          if (stszBox.getEntrySize(stszIndex) <= 2 && false){
            FAIL_MSG("size <=2");
          }else{
            long long packSendSize = 0;
            packSendSize = 24 + (BsetPart.timeOffset ? 17 : 0) + (BsetPart.bpos ? 15 : 0) + 19 +
                           stszBox.getEntrySize(stszIndex) + 11 - 2 + 19;
            meta.update(BsetPart.time, BsetPart.timeOffset, tNumber,
                        stszBox.getEntrySize(stszIndex) - 2, BsetPart.bpos, true, packSendSize);
          }
        }else{
          meta.update(BsetPart.time, BsetPart.timeOffset, tNumber,
                      stszBox.getEntrySize(stszIndex), BsetPart.bpos, BsetPart.keyframe);
        }
      }
    }

    std::string inUrl = config->getString("input");
    // Export DTSH to file
    Socket::Connection outFile;
    int tmpFd = open("/dev/null", O_RDWR);
    outFile.open(tmpFd);
    Util::Procs::socketList.insert(tmpFd);
    genericWriter(inUrl + ".dtsh", &outFile, false);
    if (outFile){M.send(outFile, false, M.getValidTracks(), false);}
    bps = 0;
    std::set<size_t> tracks = M.getValidTracks();
    for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){bps += M.getBps(*it);}
    return true;
  }

  void inputMP4::getNext(size_t idx){// get next part from track in stream
    if (curPositions.empty()){
      thisPacket.null();
      return;
    }
    // pop uit set
    mp4PartTime curPart = *curPositions.begin();
    curPositions.erase(curPositions.begin());

    bool isKeyframe = false;
    DTSC::Keys keys(M.keys(curPart.trackID));
    DTSC::Parts parts(M.parts(curPart.trackID));
    uint32_t nextKeyNum = nextKeyframe[curPart.trackID];
    if (nextKeyNum < keys.getEndValid()){
      // checking if this is a keyframe
      if (meta.getType(curPart.trackID) == "video" && curPart.time == keys.getTime(nextKeyNum)){
        isKeyframe = true;
      }
      // if a keyframe has passed, we find the next keyframe
      if (keys.getTime(nextKeyNum) <= curPart.time){
        ++nextKeyframe[curPart.trackID];
        ++nextKeyNum;
      }
    }
    if (curPart.bpos < readPos || curPart.bpos > readPos + readBuffer.size() + 512*1024 + bps){
      INFO_MSG("Buffer contains %" PRIu64 "-%" PRIu64 ", but we need %" PRIu64 "; seeking!", readPos, readPos + readBuffer.size(), curPart.bpos);
      readBuffer.truncate(0);
      if (!inFile.seek(curPart.bpos)){
        FAIL_MSG("seek unsuccessful @bpos %" PRIu64 ": %s", curPart.bpos, strerror(errno));
        thisPacket.null();
        return;
      }
      readPos = inFile.getPos();
    }else{
      //If we have more than 5MiB buffered and are more than 5MiB into the buffer, shift the first 4MiB off the buffer.
      //This prevents infinite growth of the read buffer for large files
      if (readBuffer.size() >= 5*1024*1024 && curPart.bpos > readPos + 5*1024*1024 + bps){
        readBuffer.shift(4*1024*1024);
        readPos += 4*1024*1024;
      }
    }

    while (readPos+readBuffer.size() < curPart.bpos+curPart.size && inFile && keepRunning()){
      inFile.readSome((curPart.bpos+curPart.size) - (readPos+readBuffer.size()), *this);
    }
    if (readPos+readBuffer.size() < curPart.bpos+curPart.size){
      FAIL_MSG("Read unsuccessful at %" PRIu64 ", seeking to retry...", readPos+readBuffer.size());
      readBuffer.truncate(0);
      if (!inFile.seek(curPart.bpos)){
        FAIL_MSG("seek unsuccessful @bpos %" PRIu64 ": %s", curPart.bpos, strerror(errno));
        thisPacket.null();
        return;
      }
      readPos = inFile.getPos();
      while (readPos+readBuffer.size() < curPart.bpos+curPart.size && inFile && keepRunning()){
        inFile.readSome((curPart.bpos+curPart.size) - (readPos+readBuffer.size()), *this);
      }
      if (readPos+readBuffer.size() < curPart.bpos+curPart.size){
        FAIL_MSG("Read retry unsuccessful at %" PRIu64 ", aborting", readPos+readBuffer.size());
        thisPacket.null();
        return;
      }
    }

    if (M.getCodec(curPart.trackID) == "subtitle"){
      unsigned int txtLen = Bit::btohs(readBuffer + (curPart.bpos-readPos));
      if (!txtLen && false){
        curPart.index++;
        return getNext(idx);
      }
      static JSON::Value thisPack;
      thisPack.null();
      thisPack["trackid"] = (uint64_t)curPart.trackID;
      thisPack["bpos"] = curPart.bpos; //(long long)fileSource.tellg();
      thisPack["data"] = std::string(readBuffer + (curPart.bpos-readPos) + 2, txtLen);
      thisPack["time"] = curPart.time;
      if (curPart.duration){thisPack["duration"] = curPart.duration;}
      thisPack["keyframe"] = true;
      std::string tmpStr = thisPack.toNetPacked();
      thisPacket.reInit(tmpStr.data(), tmpStr.size());
    }else{
      thisPacket.genericFill(curPart.time, curPart.offset, curPart.trackID, readBuffer + (curPart.bpos-readPos), curPart.size, 0, isKeyframe);
      INFO_MSG("Packet: %" PRIu64 ", track %zu, %" PRIu32 "b", curPart.time, curPart.trackID, curPart.size);
    }
    thisTime = curPart.time;
    thisIdx = curPart.trackID;

    // get the next part for this track
    curPart.index++;
    if (curPart.index < headerData(M.getID(curPart.trackID)).size()){
      headerData(M.getID(curPart.trackID)).getPart(curPart.index, curPart.bpos);
      curPart.size = parts.getSize(curPart.index);
      curPart.offset = parts.getOffset(curPart.index);
      curPart.time = M.getPartTime(curPart.index, thisIdx);
      curPart.duration = parts.getDuration(curPart.index);
      curPositions.insert(curPart);
    }
  }

  void inputMP4::seek(uint64_t seekTime, size_t idx){// seek to a point
    nextKeyframe.clear();
    curPositions.clear();
    if (idx != INVALID_TRACK_ID){
      handleSeek(seekTime, idx);
    }else{
      std::set<size_t> tracks = M.getValidTracks();
      for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){
        handleSeek(seekTime, *it);
      }
    }
  }

  void inputMP4::handleSeek(uint64_t seekTime, size_t idx){
    nextKeyframe[idx] = 0;
    mp4PartTime addPart;
    addPart.trackID = idx;
    // for all stsz samples in those tracks
    mp4TrackHeader &thisHeader = headerData(M.getID(idx));
    size_t headerDataSize = thisHeader.size();
    DTSC::Keys keys(M.keys(idx));
    DTSC::Parts parts(M.parts(idx));
    for (size_t i = 0; i < headerDataSize; i++){

      thisHeader.getPart(i, addPart.bpos);
      addPart.size = parts.getSize(i);
      addPart.offset = parts.getOffset(i);
      addPart.time = M.getPartTime(i, idx);
      addPart.duration = parts.getDuration(i);

      if (keys.getTime(nextKeyframe[idx]) < addPart.time){nextKeyframe[idx]++;}
      if (addPart.time >= seekTime){
        addPart.index = i;
        curPositions.insert(addPart);
        break;
      }
    }
  }
}// namespace Mist
