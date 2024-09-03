#include "mp4_stream.h"
#include "h264.h"
#include "mp4_dash.h"


namespace MP4{

  Stream::Stream(){
  }

  Stream::~Stream(){
  }

  void Stream::open(Util::ResizeablePointer & ptr){
    
  }

  bool Stream::hasPacket(size_t tid) const{
    return false;
  }

  bool Stream::hasPacket() const{
    return !curPositions.empty();
  }

  void Stream::getPacket(size_t tid, DTSC::Packet &pack, uint64_t &thisTime, size_t &thisIdx){
  }

  uint32_t Stream::getEarliestPID(){
    return INVALID_TRACK_ID;
  }

  void Stream::getEarliestPacket(DTSC::Packet &pack, uint64_t &thisTime, size_t &thisIdx){
    if (curPositions.empty()){
      pack.null();
      return;
    }
    // pop uit set
    MP4::PartTime curPart = *curPositions.begin();
    curPositions.erase(curPositions.begin());

    thisTime = curPart.time;
    thisIdx = curPart.trackID;
    pack.genericFill(curPart.time, curPart.offset, curPart.trackID, 0/*readBuffer + (curPart.bpos-readPos)*/, curPart.size, 0, curPart.keyframe);

    // get the next part for this track
    curPart.index++;
    if (curPart.index < trkHdrs[curPart.trackID].size()){
      trkHdrs[curPart.trackID].getPart(curPart.index, &curPart.bpos, &curPart.size, &curPart.time, &curPart.offset, &curPart.keyframe);
      curPositions.insert(curPart);
    }
  }

  void Stream::initializeMetadata(DTSC::Meta &meta, size_t tid, size_t mappingId){
  }

  TrackHeader::TrackHeader(){
    timeIndex = timeSample = timeFirstSample = timeTotal = timeExtra = 0;
    bposIndex = bposSample = 0;
    offsetIndex = offsetSample = 0;
    keyIndex = keySample = 0;
    hasOffsets = false;
    hasKeys = false;
    isVideo = false;
    sttsBox.clear();
    cttsBox.clear();
    stszBox.clear();
    stcoBox.clear();
    co64Box.clear();
    stscBox.clear();
    stssBox.clear();
    trexBox.clear();
    trexPtr = 0;
    stco64 = false;
    trafMode = false;
    trackId = 0;
  }

  void TrackHeader::nextMoof(){
    timeIndex = timeSample = timeFirstSample = timeTotal = timeExtra = 0;
    bposIndex = bposSample = 0;
    offsetIndex = offsetSample = 0;

    trafMode = true;
    trafs.clear();
  }

  /// Switch back to non-moof reading mode, disabling TRAF mode and wiping all TRAF boxes
  void TrackHeader::revertToMoov(){
    timeIndex = timeSample = timeFirstSample = timeTotal = timeExtra = 0;
    bposIndex = bposSample = 0;
    offsetIndex = offsetSample = 0;
    keyIndex = keySample = 0;

    trafMode = false;
    trafs.clear();
  }

  void TrackHeader::read(TREX &_trexBox){
    trexBox.copyFrom(_trexBox);
    if (trexBox.isType("trex")){trexPtr = &trexBox;}
  }

  void TrackHeader::read(TRAK &trakBox){
    vidWidth = vidHeight = audChannels = audRate = audSize = 0;
    codec.clear();

    MDIA mdiaBox = trakBox.getChild<MDIA>();
    timeScale = mdiaBox.getChild<MDHD>().getTimeScale();
    lang = mdiaBox.getChild<MP4::MDHD>().getLanguage();

    TKHD tkhd = trakBox.getChild<TKHD>();
    trackId = tkhd.getTrackID();
    if (tkhd.getWidth()){
      vidWidth = tkhd.getWidth();
      vidHeight = tkhd.getHeight();
    }

    STBL stblBox = mdiaBox.getChild<MINF>().getChild<STBL>();

    sttsBox.copyFrom(stblBox.getChild<STTS>());

    cttsBox.copyFrom(stblBox.getChild<CTTS>());
    hasOffsets = cttsBox.isType("ctts");

    stszBox.copyFrom(stblBox.getChild<STSZ>());

    stcoBox.copyFrom(stblBox.getChild<STCO>());
    co64Box.copyFrom(stblBox.getChild<CO64>());
    stco64 = co64Box.isType("co64");

    stscBox.copyFrom(stblBox.getChild<STSC>());

    stssBox.copyFrom(stblBox.getChild<STSS>());
    hasKeys = stssBox.isType("stss");

    Box sEntryBox = stblBox.getChild<MP4::STSD>().getEntry(0);
    sType = sEntryBox.getType();

    std::string handler = mdiaBox.getChild<MP4::HDLR>().getHandlerType();
    isVideo = false;
    if (handler == "vide"){
      isVideo = true;
      trackType = "video";
    }else if (handler == "soun"){
      trackType = "audio";
    }else if (handler == "sbtl"){
      trackType = "meta";
    }else{
      INFO_MSG("Unsupported handler: %s", handler.c_str());
    }

    isCompatible = false;

    if (sType == "avc1" || sType == "h264" || sType == "mp4v"){
      codec = "H264";
      isCompatible = true;
      VisualSampleEntry &vEntryBox = (VisualSampleEntry &)sEntryBox;
      if (!vidWidth){
        vidWidth = vEntryBox.getWidth();
        vidHeight = vEntryBox.getHeight();
      }
      MP4::Box initBox = vEntryBox.getCLAP();
      if (initBox.isType("avcC")){initData.assign(initBox.payload(), initBox.payloadSize());}
      initBox = vEntryBox.getPASP();
      if (initBox.isType("avcC")){initData.assign(initBox.payload(), initBox.payloadSize());}
      // Read metadata from init data if not set
      if (!vidWidth){
        h264::initData iData(initData);
        if (iData) {
          vidWidth = iData.width;
          vidHeight = iData.height;
        }
      }
    }
    if (sType == "hev1" || sType == "hvc1"){
      codec = "HEVC";
      isCompatible = true;
      MP4::VisualSampleEntry &vEntryBox = (MP4::VisualSampleEntry &)sEntryBox;
      if (!vidWidth){
        vidWidth = vEntryBox.getWidth();
        vidHeight = vEntryBox.getHeight();
      }
      MP4::Box initBox = vEntryBox.getCLAP();
      if (initBox.isType("hvcC")){initData.assign(initBox.payload(), initBox.payloadSize());}
      initBox = vEntryBox.getPASP();
      if (initBox.isType("hvcC")){initData.assign(initBox.payload(), initBox.payloadSize());}
    }
    if (sType == "av01"){
      codec = "AV1";
      isCompatible = true;
      MP4::VisualSampleEntry &vEntryBox = (MP4::VisualSampleEntry &)sEntryBox;
      if (!vidWidth){
        vidWidth = vEntryBox.getWidth();
        vidHeight = vEntryBox.getHeight();
      }
      MP4::Box initBox = vEntryBox.getCLAP();
      if (initBox.isType("av1C")){initData.assign(initBox.payload(), initBox.payloadSize());}
      initBox = vEntryBox.getPASP();
      if (initBox.isType("av1C")){initData.assign(initBox.payload(), initBox.payloadSize());}
    }
    if (sType == "vp08") {
      codec = "VP8";
      isCompatible = true;
      MP4::VisualSampleEntry & vEntryBox = (MP4::VisualSampleEntry &)sEntryBox;
      if (!vidWidth) {
        vidWidth = vEntryBox.getWidth();
        vidHeight = vEntryBox.getHeight();
      }
    }
    if (sType == "vp09") {
      codec = "VP9";
      isCompatible = true;
      MP4::VisualSampleEntry & vEntryBox = (MP4::VisualSampleEntry &)sEntryBox;
      if (!vidWidth) {
        vidWidth = vEntryBox.getWidth();
        vidHeight = vEntryBox.getHeight();
      }
    }
    if (sType == "mp4a" || sType == "aac " || sType == "ac-3" || sType == "Opus") {
      MP4::AudioSampleEntry &aEntryBox = (MP4::AudioSampleEntry &)sEntryBox;
      audRate = aEntryBox.getSampleRate();
      audChannels = aEntryBox.getChannelCount();
      audSize = 16; /// \TODO Actually get this from somewhere, probably..?

      if (sType == "ac-3"){
        codec = "AC3";
        isCompatible = true;
      }else{
        MP4::Box codingBox = aEntryBox.getCodecBox();
        if (codingBox.getType() == "esds"){
          MP4::ESDS & esdsBox = (MP4::ESDS &)codingBox;
          codec = esdsBox.getCodec();
          isCompatible = true;
          initData = esdsBox.getInitData();
        }
        if (codingBox.getType() == "wave"){
          MP4::WAVE & waveBox = (MP4::WAVE &)codingBox;
          for (size_t c = 0; c < waveBox.getContentCount(); ++c){
            MP4::Box content = waveBox.getContent(c);
            if (content.getType() == "esds"){
              MP4::ESDS & esdsBox = (MP4::ESDS &)content;
              codec = esdsBox.getCodec();
              isCompatible = true;
              initData = esdsBox.getInitData();
            }
          }
        }
        if (codingBox.getType() == "dOps") {
          MP4::DOPS & dopsBox = (MP4::DOPS &)codingBox;
          codec = "opus";
          isCompatible = true;
          initData = dopsBox.toInit();
        }
      }
    }
    if (sType == "tx3g"){// plain text subtitles
      codec = "subtitle";
      isCompatible = true;
    }
  }

  void TrackHeader::read(TRAF &trafBox){
    if (!trafMode){
      // Warn anyone that forgot to call nextMoof(), hopefully preventing future issues
      WARN_MSG("Reading TRAF box header without signalling start of next MOOF box first!");
    }
    TRAF tBox;
    trafs.push_back(tBox);
    trafs.rbegin()->copyFrom(trafBox);
  }

  void TrackHeader::increaseTime(uint32_t delta){
    // Calculate millisecond-time for current timestamp
    uint64_t timePrev = (timeTotal * 1000) / timeScale;
    timeTotal += delta;
    
    //Undo time shifts as much as possible
    if (timeExtra){
      timeTotal -= timeExtra;
      timeExtra = 0;
    }

    //Make sure our timestamps go up by at least 1ms for every packet
    if (timePrev >= (uint64_t)((timeTotal * 1000) / timeScale)){
      uint32_t wantSamples = ((timePrev+1) * timeScale) / 1000;
      timeExtra += wantSamples - timeTotal;
      timeTotal = wantSamples;
    }
    ++timeSample;
  }


  uint64_t TrackHeader::size() const {
    if (!trafMode){
      return (stszBox ? stszBox.getSampleCount() : 0);
    }
    if (!trafs.size()){return 0;}
    uint64_t parts = 0;
    for (std::deque<TRAF>::const_iterator t = trafs.begin(); t != trafs.end(); ++t){
      std::deque<TRUN> runs = ((TRAF)(*t)).getChildren<TRUN>();
      for (std::deque<TRUN>::const_iterator r = runs.begin(); r != runs.end(); ++r){
        parts += r->getSampleInformationCount();
      }
    }
    return parts;
  }

  /// Retrieves the information associated with a specific part (=frame).
  /// The index is the zero-based part number, all other arguments are optional and if non-zero will be filled.
  void TrackHeader::getPart(uint64_t index, uint64_t * byteOffset, uint32_t * byteLen, uint64_t * time, int32_t * timeOffset, bool * keyFrame, uint64_t moofPos){
    // Switch between reading TRAF boxes or global headers
    if (!trafMode){
      // Reading global headers

      // Calculate time, if requested
      if (time){
        // If we went backwards, reset our current position
        if (index < timeSample){
          timeIndex = timeFirstSample = timeSample = timeExtra = timeTotal = 0;
        }
        // Find the packet count per chunk entry for this sample
        uint64_t eCnt = sttsBox.getEntryCount();
        STTSEntry entry;
        while (timeIndex < eCnt){
          entry = sttsBox.getSTTSEntry(timeIndex);
          // check where the next index starts
          uint64_t nextSampleIndex = timeFirstSample + entry.sampleCount;
          // If the next chunk starts with a higher sample than we want, we can stop here
          if (nextSampleIndex > index){break;}
          timeFirstSample = nextSampleIndex;
          // Increase timestamp by delta for each sample with the same delta
          while (timeSample < nextSampleIndex){increaseTime(entry.sampleDelta);}
          ++timeIndex;
        }

        // Inside the samples with the same delta, we may still need to increase the timestamp.
        while (timeSample < index){increaseTime(entry.sampleDelta);}
        *time = (timeTotal * 1000) / timeScale;
      }

      // Look up time offset, if requested and available
      if (timeOffset){
        if (hasOffsets){
          // If we went backwards, reset our current position
          if (index < offsetSample){
            offsetIndex = offsetSample = 0;
          }
          // Find the packet count per chunk entry for this sample
          uint64_t eCnt = cttsBox.getEntryCount();
          CTTSEntry entry;
          while (offsetIndex < eCnt){
            entry = cttsBox.getCTTSEntry(offsetIndex);
            // check where the next index starts
            uint64_t nextSampleIndex = offsetSample + entry.sampleCount;
            // If the next chunk starts with a higher sample than we want, we can stop here
            if (nextSampleIndex > index){break;}
            offsetSample = nextSampleIndex;
            ++offsetIndex;
          }
          *timeOffset = (entry.sampleOffset * 1000) / timeScale;
        }else{
          // Default to zero if there are no offsets for this track
          *timeOffset = 0;
        }
      }

      // Look up keyframe-ness, if requested and available
      if (keyFrame){
        if (!isVideo){
          // Non-video tracks are never keyframes
          *keyFrame = false;
        }else{
          // Video tracks with keys follow them
          if (hasKeys){
            // If we went backwards, reset our current position
            if (index < keySample){
              keyIndex = keySample = 0;
            }
            // Find the packet count per chunk entry for this sample
            uint64_t eCnt = stssBox.getEntryCount();
            while (keyIndex < eCnt){
              // check where the next index starts
              uint64_t nextSampleIndex;
              if (keyIndex + 1 < eCnt){
                nextSampleIndex = stssBox.getSampleNumber(keyIndex + 1) - 1;
              }else{
                nextSampleIndex = stszBox.getSampleCount();
              }
              // If the next key has a higher sample than we want, we can stop here
              if (nextSampleIndex > index){break;}
              keySample = nextSampleIndex;
              ++keyIndex;
            }
            *keyFrame = (keySample == index);
          }else{
            // Everything is a keyframe if there are no keys listed for a video track
            *keyFrame = true;
          }
        }
      }

      // Calculate byte position of packet, if requested
      if (byteOffset){
        // If we went backwards, reset our current position
        if (index < bposSample){
          bposIndex = bposSample = 0;
        }
        // Find the packet count per chunk entry for this sample
        uint64_t eCnt = stscBox.getEntryCount();
        STSCEntry entry;
        while (bposIndex < eCnt){
          entry = stscBox.getSTSCEntry(bposIndex);
          // check where the next index starts
          uint64_t nextSampleIndex;
          if (bposIndex + 1 < eCnt){
            nextSampleIndex = bposSample + (stscBox.getSTSCEntry(bposIndex + 1).firstChunk - entry.firstChunk) *
                                                entry.samplesPerChunk;
          }else{
            nextSampleIndex = stszBox.getSampleCount();
          }
          // If the next chunk starts with a higher sample than we want, we can stop here
          if (nextSampleIndex > index){break;}
          bposSample = nextSampleIndex;
          ++bposIndex;
        }

        // Find the chunk index the sample is in
        uint64_t chunkIndex = (entry.firstChunk - 1) + ((index - bposSample) / entry.samplesPerChunk);
        // Set offset to position of start of this chunk
        *byteOffset = (stco64 ? co64Box.getChunkOffset(chunkIndex) : stcoBox.getChunkOffset(chunkIndex));
        // Increase the offset by all samples in the chunk we already passed to arrive at our current sample
        uint64_t sampleStart = bposSample + (chunkIndex - (entry.firstChunk - 1)) * entry.samplesPerChunk;
        for (int j = sampleStart; j < index; j++){*byteOffset += stszBox.getEntrySize(j);}
      }

      // Look up byte length of packet, if requested
      if (byteLen){
        *byteLen = stszBox.getEntrySize(index);
      }

      // Specifically for text tracks, remove the 2-byte header if possible
      if (byteOffset && byteLen && *byteLen >= 2 && sType == "tx3g"){
        *byteLen -= 2;
        *byteOffset += 2;
      }
    }else{
      // Reading from TRAF boxes
      size_t skipped = 0;
      for (std::deque<TRAF>::const_iterator t = trafs.begin(); t != trafs.end(); ++t){
        size_t firstTRAFIndex = skipped;
        std::deque<TRUN> runs = ((TRAF)(*t)).getChildren<TRUN>();
        for (std::deque<TRUN>::const_iterator r = runs.begin(); r != runs.end(); ++r){
          uint32_t count = r->getSampleInformationCount();
          if (index >= skipped + count){
            skipped += count;
            continue;
          }
          // Okay, our index is inside this TRUN!
          // Let's pull the TFHD box into this as well...
          TFHD tfhd = ((TRAF)(*t)).getChild<TFHD>();
          trunSampleInformation si = r->getSampleInformation(index - skipped, &tfhd, trexPtr);
          if (byteOffset){
            size_t offset = 0;
            if (tfhd.getDefaultBaseIsMoof()){
              offset += moofPos;
            }
            if (r->getFlags() & MP4::trundataOffset){
              offset += r->getDataOffset();
              size_t target = index - skipped;
              for (size_t i = 0; i < target; ++i){
                offset += r->getSampleInformation(i, &tfhd, trexPtr).sampleSize;
              }
            }else{
              FAIL_MSG("Unimplemented: trun box does not contain a data offset!");
            }
            *byteOffset = offset;
          }
          if (time){
            // If we went backwards, reset our current position
            if (!index || index < timeSample){
              timeIndex = timeFirstSample = timeSample = timeExtra = 0;
              TFDT tfdt = ((TRAF)(*t)).getChild<TFDT>();
              timeTotal = tfdt.getBaseMediaDecodeTime();
            }
            std::deque<TRUN>::const_iterator runIt = runs.begin();
            uint32_t locCount = runIt->getSampleInformationCount();
            size_t locSkipped = firstTRAFIndex;
            while (timeSample < index){
              // Most common case: timeSample is in the current TRUN box
              if (timeSample >= skipped && timeSample < skipped + count){
                trunSampleInformation i = r->getSampleInformation(timeSample - skipped, &tfhd, trexPtr);
                increaseTime(i.sampleDuration);
                continue;
              }
              // Less common case: everything else
              // Ensure "runIt" points towards the TRUN box that index "timeSample" is in
              while (timeSample >= locSkipped + locCount && runIt != runs.end()){
                locSkipped += locCount;
                runIt++;
                locCount = runIt->getSampleInformationCount();
              }
              // Abort increase if we can't find the box. This _should_ never happen...
              if (runIt == runs.end()){
                WARN_MSG("Attempted to read time information from a TRAF box that did not contain the sample we're reading!");
                break;
              }
              // Cool, now we know it's valid, increase the time accordingly.
              trunSampleInformation i = runIt->getSampleInformation(timeSample - locSkipped, &tfhd, trexPtr);
              increaseTime(i.sampleDuration);
            }
            *time = (timeTotal * 1000) / timeScale;
          }
          if (byteLen){
            *byteLen = si.sampleSize;
          }
          if (timeOffset){
            *timeOffset = (si.sampleOffset * 1000) / timeScale;
          }
          if (keyFrame){
            *keyFrame = !(si.sampleFlags & MP4::noKeySample);
          }
          return;
        }
      }
    }

  }


} // namespace MP4

