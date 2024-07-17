#include "output_mp4.h"
#include <mist/bitfields.h>
#include <mist/checksum.h>
#include <mist/defines.h>
#include <mist/encode.h>
#include <mist/mp4.h>
#include <mist/mp4_dash.h>
#include <mist/mp4_encryption.h>
#include <mist/mp4_generic.h>
#include <mist/stream.h> /* for `Util::codecString()` when streaming mp4 over websockets and playback using media source extensions. */
#include <mist/nal.h>
#include <inttypes.h>
#include <fstream>

std::set<std::string> supportedAudio;
std::set<std::string> supportedVideo;

namespace Mist{
  std::string toUTF16MP4(const std::string &original){
    std::stringstream result;
    result << (char)0xFF << (char)0xFE;
    for (std::string::const_iterator it = original.begin(); it != original.end(); it++){
      result << *it << (char)0x00;
    }
    return result.str();
  }

  SortSet::SortSet(){
    entries = 0;
    currBegin = 0;
    hasBegin = false;
  }

  /// Finds the current beginning of the SortSet
  void SortSet::findBegin(){
    if (!entries){return;}
    currBegin = 0;
    hasBegin = false;
    for (size_t i = 0; i < entries; ++i){
      if (!hasBegin && avail[i]){
        currBegin = i;
        hasBegin = true;
        continue;
      }
      if (avail[i] && ((keyPart*)(void*)ptr)[i] < ((keyPart*)(void*)ptr)[currBegin]){
        currBegin = i;
      }
    }
  }

  /// Returns a reference to the current beginning of the SortSet
  const keyPart & SortSet::begin(){
    static const keyPart blank = {0, 0, 0, 0};
    if (!hasBegin){return blank;}
    return ((keyPart*)(void*)ptr)[currBegin];
  }

  /// Marks the current beginning of the SortSet as erased
  void SortSet::erase(){
    if (!hasBegin){return;}
    avail[currBegin] = 0;
    findBegin();
  }

  bool SortSet::empty(){return !hasBegin;}

  void SortSet::insert(const keyPart & part){
    size_t i = 0;
    for (i = 0; i < entries; ++i){
      if (!avail[i] || ((keyPart*)(void*)ptr)[i].trackID == part.trackID){
        ((keyPart*)(void*)ptr)[i] = part;
        avail[i] = 1;
        if (!hasBegin || part < begin()){
          currBegin = i;
          hasBegin = true;
        }
        return;
      }
    }
    entries = i+1;
    ptr.allocate(sizeof(keyPart)*entries);
    ((keyPart*)(void*)ptr)[i] = part;
    avail.append("\001", 1);
    if (!hasBegin || part < begin()){
      currBegin = i;
      hasBegin = true;
    }
  }



  std::string OutMP4::protectionHeader(size_t idx){
    std::string tmp = toUTF16MP4(M.getPlayReady(idx));
    tmp.erase(0, 2); // remove first 2 characters
    std::stringstream resGen;
    resGen << (char)((tmp.size() + 10) & 0xFF);
    resGen << (char)(((tmp.size() + 10) >> 8) & 0xFF);
    resGen << (char)(((tmp.size() + 10) >> 16) & 0xFF);
    resGen << (char)(((tmp.size() + 10) >> 24) & 0xFF);
    resGen << (char)0x01 << (char)0x00;
    resGen << (char)0x01 << (char)0x00;
    resGen << (char)((tmp.size()) & 0xFF);
    resGen << (char)(((tmp.size()) >> 8) & 0xFF);
    resGen << tmp;
    return Encodings::Base64::encode(resGen.str());
  }

  OutMP4::OutMP4(Socket::Connection &conn) : HTTPOutput(conn){
    wsCmds = true;
    sending3GP = false;
    nextHeaderTime = 0xffffffffffffffffull;
    startTime = 0;
    endTime = 0xffffffffffffffffull;
    realBaseOffset = 1;
    timeOffset = 0;
  }
  OutMP4::~OutMP4(){}

  void OutMP4::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "MP4";
    capa["friendly"] = "MP4 over HTTP";
    capa["desc"] = "Pseudostreaming in MP4 format over HTTP";
    capa["url_match"][0u] = "/$.mp4";
    capa["url_match"][1u] = "/$.3gp";
    capa["url_match"][2u] = "/$.fmp4";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("AV1");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("AC3");
    jsonForEach(capa["codecs"][0u][0u], i){supportedVideo.insert(i->asStringRef());}
    jsonForEach(capa["codecs"][0u][1u], i){supportedAudio.insert(i->asStringRef());}
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/mp4";
    capa["methods"][0u]["hrn"] = "MP4 progressive";
    capa["methods"][0u]["priority"] = 9;
    capa["methods"][0u]["url_rel"] = "/$.mp4";
    capa["methods"][1u]["handler"] = "ws";
    capa["methods"][1u]["type"] = "ws/video/mp4";
    capa["methods"][1u]["hrn"] = "MP4 WebSocket";
    capa["methods"][1u]["priority"] = 10;
    capa["methods"][1u]["url_rel"] = "/$.mp4";

    capa["push_urls"].append("/*.fmp4");
    capa["push_urls"].append("/*.mp4");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store (f)MP4 file as, or - for stdout.";
    cfg->addOption("target", opt);

    // MP4 live is broken on Apple
    capa["exceptions"]["live"] =
        JSON::fromString("[[\"blacklist\",[\"iPad\",\"iPhone\",\"iPod\",\"Safari\"]], "
                         "[\"whitelist\",[\"Chrome\",\"Chromium\"]]]");
    capa["exceptions"]["codec:MP3"] =
        JSON::fromString("[[\"blacklist\",[\"Windows NT 5\", \"Windows NT 6.0\", \"Windows NT "
                         "6.1\"]],[\"whitelist_except\",[\"Trident\"]]]");
  }

  uint64_t OutMP4::estimateFileSize() const{
    uint64_t retVal = 0;
    for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin(); it != userSelect.end(); it++){
      DTSC::Keys keys = M.getKeys(it->first);
      size_t endKey = keys.getEndValid();
      for (size_t i = keys.getFirstValid(); i < endKey; i++){
        retVal += keys.getSize(i); // Handle number as index, faster for VoD
      }
    }
    return retVal * 1.1;
  }

  uint64_t OutMP4::mp4moofSize(uint64_t startFragmentTime, uint64_t endFragmentTime, uint64_t &mdatSize, std::map<size_t, DTSC::Keys *> & keysCache) const{
    size_t totalCount = 0;
    size_t totalSize = 8;
    uint64_t tmpRes = 8; // moof boxsize
    tmpRes += 16;        // MFHD Box

    if (endFragmentTime == 0){
      uint32_t mainTrack = M.mainTrack();
      if (mainTrack == INVALID_TRACK_ID){return 0;}
      endFragmentTime =
          M.getTimeForFragmentIndex(mainTrack, M.getFragmentIndexForTime(mainTrack, startFragmentTime) + 1);
    }

    for (std::map<size_t, Comms::Users>::const_iterator subIt = userSelect.begin();
         subIt != userSelect.end(); subIt++){
      tmpRes += 8 + 20 + 20; // TRAF + TFHD + TFDT Box

      DTSC::Keys & keys = *keysCache.at(subIt->first);
      DTSC::Parts parts(M.parts(subIt->first));

      uint32_t startKey = keys.getIndexForTime(startFragmentTime);
      uint32_t endKey = keys.getIndexForTime(endFragmentTime) + 1;

      size_t thisCount = 0;

      for (size_t k = startKey; k < endKey; k++){
        size_t firstPart = keys.getFirstPart(k);
        size_t partCount = keys.getParts(k);
        size_t endPart = firstPart + partCount;
        uint64_t timeStamp = keys.getTime(k);

        if (timeStamp >= endFragmentTime){break;}

        for (size_t p = firstPart; p < endPart; p++){
          if (timeStamp >= startFragmentTime){
            totalSize += parts.getSize(p);
            ++totalCount;
            ++thisCount;
          }

          timeStamp += parts.getDuration(p);

          if (timeStamp >= endFragmentTime){break;}
        }
      }
      if (M.getEncryption(subIt->first) != ""){
        INFO_MSG("Taking into account %zu trun box entries", thisCount);
        size_t saizSize = 12 + 5 + thisCount;
        size_t saioSize = 12 + 4 + 4; // Always 1 entry in our outputs
        size_t sencSize = 12 + 4 + ((2 /*entryCount*/ + 8 /*ivec*/ + 2 /*clear*/ + 4 /*enc*/) * thisCount);
        size_t uuidSize = sencSize + 16; // Flags is never & 0x01 in our outputs, so just an extra 8 bytes
        INFO_MSG("Sizes: Saiz %zu, Saio %zu, Senc %zu, UUID %zu", saizSize, saioSize, sencSize, uuidSize);
        tmpRes += saizSize + saioSize + sencSize + uuidSize;
      }
    }

    mdatSize = totalSize;
    tmpRes += (totalCount * 36); // Separate TRUN Box for each part
    return tmpRes;
  }

  uint64_t OutMP4::mp4HeaderSize(uint64_t &fileSize, int fragmented) const{
    bool useLargeBoxes = !fragmented && (estimateFileSize() > 0xFFFFFFFFull);
    uint64_t res = 36 + 8 + 108; // FTYP + MOOV + MVHD Boxes
    uint64_t firstms = 0xFFFFFFFFFFFFFFFFull;
    for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin(); it != userSelect.end(); it++){
      firstms = std::min(firstms, M.getFirstms(it->first));
    }
    for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin(); it != userSelect.end(); it++){
      const std::string tType = M.getType(it->first);
      uint64_t tmpRes = 0;
      DTSC::Keys keys = M.getKeys(it->first);
      DTSC::Parts parts(M.parts(it->first));
      uint64_t partCount = keys.getTotalPartCount();

      tmpRes += 8 + 92                                        // TRAK + TKHD Boxes
                + 36                                          // EDTS Box
                + 8 + 32                                      // MDIA + MDHD Boxes
                + 33 + M.getTrackIdentifier(it->first).size() // HDLR Box
                + 8                                           // MINF Box
                + 36                                          // DINF Box
                + 8;                                          // STBL Box
      if (!M.getLive() && M.getFirstms(it->first) != firstms){
        tmpRes += 12; // EDTS entry extra
      }

      // These boxes are empty when generating fragmented output
      tmpRes += 20 + (fragmented ? 0 : (partCount * 4));                       // STSZ
      tmpRes += 16 + (fragmented ? 0 : (partCount * (useLargeBoxes ? 8 : 4))); // STCO
      tmpRes += 16 + (fragmented ? 0 : (1 * 12)); // STSC <-- Currently 1 entry, but might become more complex in near future

      // Type-specific boxes
      if (tType == "video"){
        tmpRes += 20                                 // VMHD Box
                  + 16                               // STSD
                  + 86                               // AVC1
                  + 16                               // PASP
                  + 8 + M.getInit(it->first).size(); // avcC
        if (!fragmented){
          tmpRes += 16 + (keys.getValidCount() * 4); // STSS
        }
      }
      if (tType == "audio"){
        tmpRes += 16   // SMHD Box
                  + 16 // STSD
                  + 36 // MP4A
                  + 35;
        if (M.getInit(it->first).size()){
          tmpRes += 2 + M.getInit(it->first).size(); // ESDS - Content expansion
        }
      }

      if (tType == "meta"){
        tmpRes += 12    // NMHD Box
                  + 16  // STSD
                  + 64; // tx3g Box
      }

      if (!fragmented){
        // Unfortunately, for our STTS and CTTS boxes, we need to loop through all parts of the
        // track
        size_t firstPart = keys.getFirstPart(keys.getFirstValid());
        uint64_t sttsCount = 1;
        uint64_t prevDur = parts.getDuration(firstPart);
        uint64_t prevOffset = parts.getOffset(firstPart);
        uint64_t cttsCount = 1;
        fileSize += parts.getSize(firstPart);
        bool isMeta = (tType == "meta");
        for (unsigned int part = 1; part < partCount; ++part){
          uint64_t partDur = parts.getDuration(firstPart + part);
          uint64_t partOffset = parts.getOffset(firstPart + part);
          uint64_t partSize = parts.getSize(firstPart + part)+(isMeta?2:0);
          if (prevDur != partDur){
            prevDur = partDur;
            ++sttsCount;
          }
          if (partOffset != prevOffset){
            prevOffset = partOffset;
            ++cttsCount;
          }
          fileSize += partSize;
        }
        if (cttsCount == 1 && !prevOffset){cttsCount = 0;}
        tmpRes += 16 + (sttsCount * 8); // STTS
        if (cttsCount){
          tmpRes += 16 + (cttsCount * 8); // CTTS
        }
      }else{
        tmpRes += 16; // empty STTS, no CTTS
      }

      res += tmpRes;
    }

    if (fragmented){
      res += 8 + (userSelect.size() * 32); // Mvex + trex boxes
      res += 16;                           // mehd box
    }else{
      res += 8; // mdat beginning
    }

    fileSize += res;
    return res;
  }

  class trackLookup{
    public:
      trackLookup(){
        parts = 0;
      }
      ~trackLookup(){
        if (parts){delete parts;}
      }
      void init(const Util::RelAccX & relParts, MP4::STCO box){
        parts = new DTSC::Parts(relParts);
        stcoBox = box;
      }
      void init(const Util::RelAccX & relParts, MP4::CO64 box){
        parts = new DTSC::Parts(relParts);
        co64Box = box;
      }
      DTSC::Parts * parts;
      MP4::STCO stcoBox;
      MP4::CO64 co64Box;
  };


  bool OutMP4::mp4Header(Util::ResizeablePointer & headOut, uint64_t &size, int fragmented){
    uint32_t mainTrack = M.mainTrack();
    if (mainTrack == INVALID_TRACK_ID){return false;}
    if (M.getLive()){needsLookAhead = 5000;}
    if (webSock){needsLookAhead = 0;} 
    // Clear size if it was set before the function was called, just in case
    size = 0;
    // Determines whether the outputfile is larger than 4GB, in which case we need to use 64-bit
    // boxes for offsets
    bool useLargeBoxes = !fragmented && (estimateFileSize() > 0xFFFFFFFFull);
    // Keeps track of the total size of the mdat box
    uint64_t mdatSize = 0;

    // MP4 Files always start with an FTYP box. Constructor sets default values
    MP4::FTYP ftypBox;
    if (sending3GP){
      ftypBox.setMajorBrand("3gp6");
      ftypBox.setCompatibleBrands("3gp6", 3);
    }
    headOut.append(ftypBox.asBox(), ftypBox.boxedSize());

    // Start building the moov box. This is the metadata box for an mp4 file, and will contain all
    // metadata.
    MP4::MOOV moovBox;
    // Keep track of the current index within the moovBox
    unsigned int moovOffset = 0;

    uint64_t firstms = 0xFFFFFFFFFFFFFFull;
    // Construct with duration of -1, as this is the default for fragmented
    MP4::MVHD mvhdBox(0);
    // Then override it when we are not sending a VoD asset
    if (!M.getLive()){
      // calculating longest duration
      uint64_t lastms = 0;
      for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin();
           it != userSelect.end(); it++){
        if (prevVidTrack != INVALID_TRACK_ID && it->first == prevVidTrack){continue;}
        lastms = std::max(lastms, M.getLastms(it->first));
        firstms = std::min(firstms, M.getFirstms(it->first));
      }
      mvhdBox.setDuration(lastms - firstms);
    }
    // Set the trackid for the first "empty" track within the file.
    mvhdBox.setTrackID(userSelect.size() + 1);
    moovBox.setContent(mvhdBox, moovOffset++);

    for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin(); it != userSelect.end(); it++){
      if (prevVidTrack != INVALID_TRACK_ID && it->first == prevVidTrack){continue;}
      DTSC::Parts parts(M.parts(it->first));
      DTSC::Keys keys = M.getKeys(it->first);
      size_t partCount = keys.getTotalPartCount();
      uint64_t tDuration = M.getLastms(it->first) - M.getFirstms(it->first);
      std::string tType = M.getType(it->first);

      MP4::TRAK trakBox;
      // Keep track of the current index within the moovBox
      size_t trakOffset = 0;

      MP4::TKHD tkhdBox(M, it->first);
      trakBox.setContent(tkhdBox, trakOffset++);

      // This box is used for track durations as well as firstms synchronisation;
      MP4::EDTS edtsBox;
      MP4::ELST elstBox;
      elstBox.setVersion(0);
      elstBox.setFlags(0);
      if (!M.getLive() && M.getFirstms(it->first) != firstms){
        elstBox.setCount(2);

        elstBox.setSegmentDuration(0, M.getFirstms(it->first) - firstms);
        elstBox.setMediaTime(0, 0xFFFFFFFFull);
        elstBox.setMediaRateInteger(0, 0);
        elstBox.setMediaRateFraction(0, 0);

        elstBox.setSegmentDuration(1, tDuration);
        elstBox.setMediaTime(1, 0);
        elstBox.setMediaRateInteger(1, 1);
        elstBox.setMediaRateFraction(1, 0);
      }else{
        elstBox.setCount(1);
        elstBox.setSegmentDuration(0, fragmented ? 0 : tDuration);
        elstBox.setMediaTime(0, 0);
        elstBox.setMediaRateInteger(0, 1);
        elstBox.setMediaRateFraction(0, 0);
      }
      edtsBox.setContent(elstBox, 0);
      trakBox.setContent(edtsBox, trakOffset++);

      MP4::MDIA mdiaBox;
      size_t mdiaOffset = 0;

      // Add the mandatory MDHD and HDLR boxes to the MDIA
      MP4::MDHD mdhdBox(tDuration);
      if (fragmented){mdhdBox.setDuration(0);}
      mdhdBox.setLanguage(M.getLang(it->first));
      mdiaBox.setContent(mdhdBox, mdiaOffset++);
      MP4::HDLR hdlrBox(tType, M.getTrackIdentifier(it->first));
      mdiaBox.setContent(hdlrBox, mdiaOffset++);

      MP4::MINF minfBox;
      size_t minfOffset = 0;

      // Add a track-type specific box to the MINF box
      if (tType == "video"){
        MP4::VMHD vmhdBox(0, 1);
        minfBox.setContent(vmhdBox, minfOffset++);
      }else if (tType == "audio"){
        MP4::SMHD smhdBox;
        minfBox.setContent(smhdBox, minfOffset++);
      }else{
        // create nmhd box
        MP4::NMHD nmhdBox;
        minfBox.setContent(nmhdBox, minfOffset++);
      }

      // Add the mandatory DREF (dataReference) box
      MP4::DINF dinfBox;
      MP4::DREF drefBox;
      dinfBox.setContent(drefBox, 0);
      minfBox.setContent(dinfBox, minfOffset++);

      // Add STSD box
      MP4::STSD stsdBox(0);
      if (tType == "video"){
        MP4::VisualSampleEntry sampleEntry(M, it->first);
        if (M.getEncryption(it->first) != ""){
          MP4::SINF sinfBox;

          MP4::FRMA frmaBox(sampleEntry.getCodec());
          sinfBox.setEntry(frmaBox, 0);

          sampleEntry.setCodec("encv");

          MP4::SCHM schmBox; // Defaults to CENC values
          sinfBox.setEntry(schmBox, 1);

          MP4::SCHI schiBox;

          MP4::TENC tencBox;
          std::string encryption = M.getEncryption(it->first);
          std::string kid = encryption.substr(encryption.find('/') + 1,
                                              encryption.find(':') - 1 - encryption.find('/'));
          tencBox.setDefaultKID(Encodings::Hex::decode(kid));

          schiBox.setContent(tencBox);
          sinfBox.setEntry(schiBox, 2);
          sampleEntry.setBoxEntry(2, sinfBox);
        }
        stsdBox.setEntry(sampleEntry, 0);
      }else if (tType == "audio"){
        MP4::AudioSampleEntry sampleEntry(M, it->first);
        stsdBox.setEntry(sampleEntry, 0);
      }else if (tType == "meta"){
        MP4::TextSampleEntry sampleEntry(M, it->first);

        MP4::FontTableBox ftab;
        sampleEntry.setFontTableBox(ftab);
        stsdBox.setEntry(sampleEntry, 0);
      }

      MP4::STBL stblBox;
      size_t stblOffset = 0;
      stblBox.setContent(stsdBox, stblOffset++);

      // Add STTS Box
      // note: STTS is empty when fragmented
      MP4::STTS sttsBox(0);
      // Add STSZ Box
      // note: STSZ is empty when fragmented
      MP4::STSZ stszBox(0);
      if (!fragmented){

        MP4::CTTS cttsBox;
        cttsBox.setVersion(0);

        size_t firstPart = keys.getFirstPart(keys.getFirstValid());

        size_t totalEntries = 0;
        MP4::CTTSEntry tmpEntry;
        tmpEntry.sampleCount = 0;
        tmpEntry.sampleOffset = parts.getOffset(firstPart);

        size_t sttsCounter = 0;
        MP4::STTSEntry sttsEntry;
        sttsEntry.sampleCount = 0;
        sttsEntry.sampleDelta = parts.getSize(firstPart);

        //Calculate amount of entries for CTTS/STTS boxes so we can set the last entry first
        //Our MP4 box implementations dynamically reallocate to fit the data you put inside them,
        //Which means setting the last entry first prevents constant reallocs and slowness.
        for (size_t part = 0; part < partCount; ++part){
          uint64_t partOffset = parts.getOffset(firstPart + part);
          if (partOffset != tmpEntry.sampleOffset){
            ++totalEntries;
            tmpEntry.sampleOffset = partOffset;
          }
          uint64_t partDur = parts.getDuration(firstPart + part);
          if (partDur != sttsEntry.sampleDelta){
            ++sttsCounter;
            sttsEntry.sampleDelta = partDur;
          }
        }

        //Set temporary last entry for CTTS box
        bool hasCTTS = (totalEntries || tmpEntry.sampleOffset);
        if (hasCTTS){
          cttsBox.setCTTSEntry(tmpEntry, totalEntries);
        }
        //Set temporary last entry for STTS box
        sttsBox.setSTTSEntry(sttsEntry, sttsCounter-1);
        //Set temporary last entry for STSZ box
        stszBox.setEntrySize(0, partCount - 1);

        //All set! Now we can do everything for real.
        //Reset the values we just used, first.
        totalEntries = 0;
        tmpEntry.sampleCount = 0;
        tmpEntry.sampleOffset = parts.getOffset(firstPart);
        sttsCounter = 0;
        sttsEntry.sampleCount = 0;
        sttsEntry.sampleDelta = parts.getDuration(firstPart);
        
        bool isMeta = (tType == "meta");
        for (size_t part = 0; part < partCount; ++part){
          uint64_t partDur = parts.getDuration(firstPart + part);
          if (sttsEntry.sampleDelta != partDur){
            // If the duration of this and previous part differ, write current values and reset
            sttsBox.setSTTSEntry(sttsEntry, sttsCounter++);
            sttsEntry.sampleCount = 0;
            sttsEntry.sampleDelta = partDur;
          }
          sttsEntry.sampleCount++;

          uint64_t partSize = parts.getSize(firstPart + part)+(isMeta?2:0);
          stszBox.setEntrySize(partSize, part);
          size += partSize;
          
          if (hasCTTS){
            uint64_t partOffset = parts.getOffset(firstPart + part);
            if (partOffset != tmpEntry.sampleOffset){
              // If the offset of this and previous part differ, write current values and reset
              cttsBox.setCTTSEntry(tmpEntry, totalEntries++);
              tmpEntry.sampleCount = 0;
              tmpEntry.sampleOffset = partOffset;
            }
            tmpEntry.sampleCount++;
          }
        }
        //Write last entry for STTS
        sttsBox.setSTTSEntry(sttsEntry, sttsCounter);

        //Only add the CTTS box to the STBL box if we had any entries in it
        if (hasCTTS){
          //Write last entry for CTTS
          cttsBox.setCTTSEntry(tmpEntry, totalEntries);
          stblBox.setContent(cttsBox, stblOffset++);
        }
      }
      stblBox.setContent(sttsBox, stblOffset++);
      stblBox.setContent(stszBox, stblOffset++);

      // Add STSS Box IF type is video and we are not fragmented
      if (tType == "video" && !fragmented){
        MP4::STSS stssBox(0);
        size_t tmpCount = 0;
        size_t firstKey = keys.getFirstValid();
        size_t endKey = keys.getEndValid();
        for (size_t i = firstKey; i < endKey; ++i){
          stssBox.setSampleNumber(tmpCount + 1, i-firstKey);
          tmpCount += keys.getParts(i);
        }
        stblBox.setContent(stssBox, stblOffset++);
      }

      // Add STSC Box
      // note: STSC is empty when fragmented
      MP4::STSC stscBox(0);
      if (!fragmented){
        MP4::STSCEntry stscEntry(1, 1, 1);
        stscBox.setSTSCEntry(stscEntry, 0);
      }
      stblBox.setContent(stscBox, stblOffset++);

      // Create STCO Box (either stco or co64)
      // note: 64bit boxes will never be used in fragmented
      // note: Inserting empty values on purpose here, will be fixed later.
      if (useLargeBoxes){
        MP4::CO64 CO64Box;
        CO64Box.setChunkOffset(0, partCount - 1);
        stblBox.setContent(CO64Box, stblOffset++);
      }else{
        MP4::STCO stcoBox(0);
        if (fragmented){
          stcoBox.setEntryCount(0);
        }else{
          stcoBox.setChunkOffset(0, partCount - 1);
        }
        stblBox.setContent(stcoBox, stblOffset++);
      }

      minfBox.setContent(stblBox, minfOffset++);

      mdiaBox.setContent(minfBox, mdiaOffset++);

      trakBox.setContent(mdiaBox, 2);

      moovBox.setContent(trakBox, moovOffset++);
    }

    if (fragmented){
      MP4::MVEX mvexBox;
      size_t curBox = 0;
      MP4::MEHD mehdBox;
      mehdBox.setFragmentDuration(0);

      mvexBox.setContent(mehdBox, curBox++);
      for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin();
           it != userSelect.end(); it++){
        if (prevVidTrack != INVALID_TRACK_ID && it->first == prevVidTrack){continue;}
        MP4::TREX trexBox(it->first + 1);
        trexBox.setDefaultSampleDuration(1000);
        mvexBox.setContent(trexBox, curBox++);
      }
      moovBox.setContent(mvexBox, moovOffset++);
      for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin();
           it != userSelect.end(); it++){
        if (prevVidTrack != INVALID_TRACK_ID && it->first == prevVidTrack){continue;}
        if (M.getEncryption(it->first) != ""){
          MP4::PSSH psshBox;
          psshBox.setSystemIDHex(Encodings::Hex::decode("9a04f07998404286ab92e65be0885f95"));
          psshBox.setData(Encodings::Base64::decode(protectionHeader(it->first)));
          moovBox.setContent(psshBox, moovOffset++);
          MP4::PSSH widevineBox;
          widevineBox.setSystemIDHex(Encodings::Hex::decode("edef8ba979d64acea3c827dcd51d21ed"));
          widevineBox.setData(Encodings::Base64::decode(M.getWidevine(it->first)));
          moovBox.setContent(widevineBox, moovOffset++);
        }
      }
    }else{// if we are making a non fragmented MP4 and there are parts
      // initial offset length ftyp, length moov + 8
      uint64_t dataOffset = ftypBox.boxedSize() + moovBox.boxedSize() + 8;


      QuickMap<trackLookup> trackMap;

      std::deque<MP4::TRAK> trak = moovBox.getChildren<MP4::TRAK>();
      for (std::deque<MP4::TRAK>::iterator trakIt = trak.begin(); trakIt != trak.end(); trakIt++){
        size_t idx = trakIt->getChild<MP4::TKHD>().getTrackID() - 1;
        MP4::STBL stblBox = trakIt->getChild<MP4::MDIA>().getChild<MP4::MINF>().getChild<MP4::STBL>();
        trackMap.insert(idx, trackLookup());
        if (useLargeBoxes){
          trackMap.get(idx).init(M.parts(idx), stblBox.getChild<MP4::CO64>());
        }else{
          trackMap.get(idx).init(M.parts(idx), stblBox.getChild<MP4::STCO>());
        }
      }

      // inserting right values in the STCO box header
      // total = 0;
      // Keep track of the current size of the data within the mdat
      uint64_t dataSize = 0;
      // Current values are actual byte offset without header-sized offset
      SortSet sortSet; // filling sortset for interleaving parts
      for (std::map<size_t, Comms::Users>::const_iterator subIt = userSelect.begin();
           subIt != userSelect.end(); subIt++){
        if (prevVidTrack != INVALID_TRACK_ID && subIt->first == prevVidTrack){continue;}
        keyPart temp;
        temp.trackID = subIt->first;

        DTSC::Keys keys = M.getKeys(subIt->first);
        temp.time = keys.getTime(keys.getFirstValid());
        temp.index = keys.getFirstPart(keys.getFirstValid());
        temp.firstIndex = temp.index;
        sortSet.insert(temp);
      }
      while (!sortSet.empty()){
        stats();
        keyPart temp = sortSet.begin();
        trackLookup & tL = trackMap.get(temp.trackID);
        sortSet.erase();

        DTSC::Parts & parts = *tL.parts;
        // setting the right STCO size in the STCO box
        if (useLargeBoxes){// Re-using the previously defined boolean for speedup
          tL.co64Box.setChunkOffset(dataOffset + dataSize, temp.index - temp.firstIndex);
        }else{
          tL.stcoBox.setChunkOffset(dataOffset + dataSize, temp.index - temp.firstIndex);
        }
        dataSize += parts.getSize(temp.index);

        if (M.getType(temp.trackID) == "meta"){dataSize += 2;}
        // add next keyPart to sortSet, if we have not yet reached the end time
        if (temp.time + parts.getDuration(temp.index) < M.getLastms(temp.trackID)){
          temp.time += parts.getDuration(temp.index);
          ++temp.index;
          sortSet.insert(temp);
        }
      }

      ///\todo Update for when mdat box exceeds 4GB
      mdatSize = dataSize + 8; //+8 for mp4 header
    }
    headOut.append(moovBox.asBox(), moovBox.boxedSize());

    if (!fragmented){// if we are making a non fragmented MP4 and there are parts
      char mdatHeader[8] ={0x00, 0x00, 0x00, 0x00, 'm', 'd', 'a', 't'};

      if (mdatSize < 0xFFFFFFFF){Bit::htobl(mdatHeader, mdatSize);}
      headOut.append(mdatHeader, 8);
    }
    size += headOut.size();
    if (fragmented){realBaseOffset = headOut.size();}
    return true;
  }

  /// Calculate a seekPoint, based on byteStart, metadata, tracks and headerSize.
  /// The seekPoint will be set to the timestamp of the first packet to send.
  void OutMP4::findSeekPoint(uint64_t byteStart, uint64_t &seekPoint, uint64_t headerSize){
    // if we're starting in the header, seekPoint is always zero.
    if (byteStart <= headerSize){return;}
    // okay, we're past the header. Substract the headersize from the starting postion.
    byteStart -= headerSize;
    // forward through the file by headers, until we reach the point where we need to be
    while (!sortSet.empty()){
      // find the next part and erase it
      keyPart temp = *sortSet.begin();

      DTSC::Parts parts(M.parts(temp.trackID));
      uint64_t partSize = parts.getSize(temp.index);

      // add 2 bytes in front of the subtitle that contains the length of the subtitle.
      if (M.getCodec(temp.trackID) == "subtitle"){partSize += 2;}

      // record where we are
      seekPoint = temp.time;
      // substract the size of this fragment from byteStart
      // if that put us past the point where we wanted to be, return right now
      if (partSize > byteStart){
        HIGH_MSG("We're starting at time %" PRIu64 ", skipping %" PRIu64 " bytes", seekPoint, partSize - byteStart);
        return;
      }

      byteStart -= partSize;

      // otherwise, set currPos to where we are now and continue
      currPos += partSize;

      if (temp.time + parts.getDuration(temp.index) < M.getLastms(temp.trackID)){// only insert when there are parts left
        temp.time += parts.getDuration(temp.index);
        ++temp.index;
        sortSet.insert(temp);
      }
      // Remove just-parsed element
      sortSet.erase(sortSet.begin());
      // wash, rinse, repeat
    }
    // If we're here, we're in the last fragment.
    // That's technically legal, of course.
  }

  // ------------------------------------------------------------

  size_t OutMP4::fragmentHeaderSize(std::deque<size_t>& sortedTracks, std::set<keyPart>& trunOrder, uint64_t startFragmentTime, uint64_t endFragmentTime) {
    /*
      8 = moof (once)
      16 = mfhd (once)
      
      per track:
      32 = tfhd + first 8 bytes of traf
      20 = tfdt
      24 = 24 * ... 
     */
    size_t ret = (8 + 16) + ((32 + 20 + 24) * sortedTracks.size()) + 12 * trunOrder.size();
    return ret;
  }

  // this function was created to add support for streaming mp4
  // over websockets. each time `sendNext()` is called we will
  // wrap the data into a `moof` packet and send it to the
  // webocket client.
  void OutMP4::appendSinglePacketMoof(Util::ResizeablePointer& moofOut, size_t extraBytes){

    /* 
       roxlu: I've added this check as this resulted in a
       segfault while working on the websocket api. This
       shouldn't be necessary as `sendNext()` should not be
       called when `thisPacket` is invalid. Though, having this
       here won't hurt and prevents us from running into
       segfaults.
    */
    if (!thisPacket.getData()) {
      FAIL_MSG("Current packet has no data, lookahead: %" PRIu64, needsLookAhead);
      return;
    }
    
    //INFO_MSG("-- thisPacket.getDataStrignLen(): %u", thisPacket.getDataStringLen());
    //INFO_MSG("-- appendSinglePacketMoof");

    MP4::MOOF moofBox;
    MP4::MFHD mfhdBox(fragSeqNum++);
    moofBox.setContent(mfhdBox, 0);

    MP4::TRAF trafBox;
    MP4::TFHD tfhdBox;
    size_t track = thisIdx;

    tfhdBox.setFlags(MP4::tfhdSampleFlag | MP4::tfhdBaseIsMoof | MP4::tfhdSampleDesc);
    tfhdBox.setTrackID(track + 1);
    tfhdBox.setDefaultSampleDuration(444);
    tfhdBox.setDefaultSampleSize(444);
    tfhdBox.setDefaultSampleFlags((M.getType(track) == "video") ? (MP4::noIPicture | MP4::noKeySample)
                                  : (MP4::isIPicture | MP4::isKeySample));
    tfhdBox.setSampleDescriptionIndex(1);
    trafBox.setContent(tfhdBox, 0);

    MP4::TFDT tfdtBox;
    tfdtBox.setBaseMediaDecodeTime(thisPacket.getTime());
    trafBox.setContent(tfdtBox, 1);

    MP4::TRUN trunBox;
    trunBox.setFirstSampleFlags(thisPacket.getFlag("keyframe") ? (MP4::isIPicture | MP4::isKeySample) : (MP4::noIPicture | MP4::noKeySample));
    trunBox.setFlags(MP4::trundataOffset | MP4::trunfirstSampleFlags | MP4::trunsampleSize |
                     MP4::trunsampleDuration | MP4::trunsampleOffsets);

    /*
      
       8 = moof (once)
      16 = mfhd (once)
      
      per track:
        32 = tfhd + first 8 bytes of traf
        20 = tfdt
        24 = 24 * ... 
    */
    trunBox.setDataOffset(8 + (8 + 16) + ((32 + 20 + 24)) + 12);

    MP4::trunSampleInformation sampleInfo;

    size_t part_idx = M.getPartIndex(thisPacket.getTime(), thisIdx);
    DTSC::Parts parts(M.parts(thisIdx));
    sampleInfo.sampleDuration = parts.getDuration(part_idx);

    sampleInfo.sampleOffset = 0;
    if (thisPacket.hasMember("offset")) { 
      sampleInfo.sampleOffset = thisPacket.getInt("offset");
    }

    sampleInfo.sampleSize = thisPacket.getDataStringLen()+extraBytes;

    trunBox.setSampleInformation(sampleInfo, 0);
    trafBox.setContent(trunBox, 2);
    moofBox.setContent(trafBox, 1);
    moofOut.append(moofBox.asBox(), moofBox.boxedSize());
  }

  // ------------------------------------------------------------
  
  void OutMP4::sendFragmentHeaderTime(uint64_t startFragmentTime, uint64_t endFragmentTime){
    bool hasAudio = false;
    uint64_t mdatSize = 0;
    MP4::MOOF moofBox;
    MP4::MFHD mfhdBox;
    mfhdBox.setSequenceNumber(fragSeqNum++);
    moofBox.setContent(mfhdBox, 0);
    unsigned int moofIndex = 1;
    sortSet.clear();

    std::set<keyPart> trunOrder;
    std::set<size_t> keyParts;
    std::deque<size_t> sortedTracks;

    if (endFragmentTime == 0){
      size_t mainTrack = M.mainTrack();
      if (mainTrack == INVALID_TRACK_ID){return;}
      endFragmentTime =
          M.getTimeForFragmentIndex(mainTrack, M.getFragmentIndexForTime(mainTrack, startFragmentTime) + 1);
    }
    
    std::map<size_t, DTSC::Keys *> keysCache;

    for (std::map<size_t, Comms::Users>::const_iterator subIt = userSelect.begin();
         subIt != userSelect.end(); subIt++){
      keysCache[subIt->first] = new DTSC::Keys(M.getKeys(subIt->first));
      DTSC::Keys & keys = *keysCache[subIt->first];
      DTSC::Parts parts(M.parts(subIt->first));

      uint32_t startKey = keys.getIndexForTime(startFragmentTime);
      uint32_t endKey = keys.getIndexForTime(endFragmentTime) + 1;

      for (size_t k = startKey; k < endKey; k++){

        size_t firstPart = keys.getFirstPart(k);
        size_t partCount = keys.getParts(k);
        size_t endPart = firstPart + partCount;
        uint64_t timeStamp = keys.getTime(k);

        if (timeStamp >= endFragmentTime){break;}

        // loop through parts
        for (size_t p = firstPart; p < endPart; p++){
          // add part to trunOrder when timestamp is before endFragmentTime
          if (timeStamp >= startFragmentTime){
            keyPart temp;
            temp.trackID = subIt->first;
            temp.time = timeStamp;
            temp.index = p;
            trunOrder.insert(temp);

            uint64_t keyTime = M.getTimeForKeyIndex(subIt->first, keys.getIndexForTime(timeStamp));
            if (keyTime == timeStamp){
              keyParts.insert(p);
            }
          }

          timeStamp += parts.getDuration(p);

          if (timeStamp >= endFragmentTime){break;}
        }
      }

      // Fun fact! Firefox cares about the ordering here.
      // It doesn't care about the order or track IDs in the header.
      // But - the first TRAF must be a video TRAF, if video is present.
      if (M.getType(subIt->first) == "video"){
        sortedTracks.push_front(subIt->first);
      }else{
        if (!hasAudio && M.getType(subIt->first) == "audio"){hasAudio = true;}
        sortedTracks.push_back(subIt->first);
      }
    }

    uint64_t relativeOffset = mp4moofSize(startFragmentTime, endFragmentTime, mdatSize, keysCache) + 8;

    // We need to loop over each part and fill a new set, because editing byteOffest might edit
    // relative order, and invalidates the iterator.
    for (std::set<keyPart>::iterator it = trunOrder.begin(); it != trunOrder.end(); it++){
      DTSC::Parts parts(M.parts(it->trackID));
      uint64_t partSize = parts.getSize(it->index);
      // We have to make a copy, because altering the element inside the set would invalidate the
      // iterators
      keyPart temp = *it;
      temp.byteOffset = relativeOffset;
      relativeOffset += partSize;
      DONTEVEN_MSG("Anticipating tid: %zu size: %" PRIu64, it->trackID, partSize);
      sortSet.insert(temp);
    }
    trunOrder.clear(); // erase the trunOrder set, to keep memory usage down

    for (std::deque<size_t>::iterator it = sortedTracks.begin(); it != sortedTracks.end(); ++it){
      //Wipe keys cache, no longer needed
      delete keysCache[*it];
      keysCache.erase(*it);

      size_t tid = *it;
      DTSC::Parts parts(M.parts(*it));

      MP4::TRAF trafBox;
      MP4::TFHD tfhdBox;

      tfhdBox.setFlags(MP4::tfhdSampleFlag | MP4::tfhdBaseIsMoof);
      tfhdBox.setTrackID(tid + 1);
      realBaseOffset += headerSize;

      tfhdBox.setDefaultSampleDuration(444);
      tfhdBox.setDefaultSampleSize(444);
      tfhdBox.setDefaultSampleFlags(tid == vidTrack ? (MP4::noIPicture | MP4::noKeySample)
                                                    : (MP4::isIPicture | MP4::isKeySample));
      unsigned int trafOffset = 0;
      trafBox.setContent(tfhdBox, trafOffset++);

      MP4::TFDT tfdtBox;
      tfdtBox.setBaseMediaDecodeTime(startFragmentTime - timeOffset);
      trafBox.setContent(tfdtBox, trafOffset++);


      for (std::set<keyPart>::iterator trunIt = sortSet.begin(); trunIt != sortSet.end(); trunIt++){
        if (trunIt->trackID == tid){
          DTSC::Parts parts(M.parts(trunIt->trackID));
          uint64_t partOffset = parts.getOffset(trunIt->index);
          uint64_t partSize = parts.getSize(trunIt->index);
          uint64_t partDur = parts.getDuration(trunIt->index);

          MP4::TRUN trunBox;

          trunBox.setFlags(MP4::trundataOffset | MP4::trunfirstSampleFlags | MP4::trunsampleSize |
                           MP4::trunsampleDuration | MP4::trunsampleOffsets);
          // The value set here, will be updated afterwards to the correct value
          trunBox.setDataOffset(trunIt->byteOffset);

          bool isKeyFrame = keyParts.count(trunIt->index);
          if (M.getType(trunIt->trackID) != "video"){
            isKeyFrame = true;
          }
          trunBox.setFirstSampleFlags(isKeyFrame ? (MP4::isKeySample | MP4::isIPicture) : (MP4::noKeySample | MP4::noIPicture));

          MP4::trunSampleInformation sampleInfo;
          sampleInfo.sampleSize = partSize;
          sampleInfo.sampleDuration = partDur;
          sampleInfo.sampleOffset = partOffset;
          trunBox.setSampleInformation(sampleInfo, 0);
          trafBox.setContent(trunBox, trafOffset++);
        }
      }
      moofBox.setContent(trafBox, moofIndex);
      moofIndex++;
    }

    // Oh god why do we do this.
    if (chromeWorkaround && hasAudio && fragSeqNum == 0){
      INFO_MSG("Activating Chrome MP4 compatibility workaround!");
      MP4::TRAF trafBox;
      MP4::TRUN trunBox;
      trunBox.setFlags(MP4::trundataOffset | MP4::trunfirstSampleFlags | MP4::trunsampleSize | MP4::trunsampleDuration);
      trunBox.setDataOffset(0);
      trunBox.setFirstSampleFlags(MP4::isIPicture | MP4::noKeySample);
      MP4::trunSampleInformation sampleInfo;
      sampleInfo.sampleSize = 0;
      sampleInfo.sampleDuration = -1;
      trunBox.setSampleInformation(sampleInfo, 0);
      trafBox.setContent(trunBox, 0);
      moofBox.setContent(trafBox, moofIndex);
      moofIndex++;
    }

    realBaseOffset += (moofBox.boxedSize() + mdatSize);

    H.Chunkify(moofBox.asBox(), moofBox.boxedSize(), myConn);

    char mdatHeader[8] ={0x00, 0x00, 0x00, 0x00, 'm', 'd', 'a', 't'};
    Bit::htobl(mdatHeader, mdatSize);
    H.Chunkify(mdatHeader, 8, myConn);
  }

  void OutMP4::respondHTTP(const HTTP::Parser & req, bool headersOnly){
    //Set global defaults, first
    HTTPOutput::respondHTTP(req, headersOnly);
    initialSeek();

    H.SetHeader("Content-Type", "video/MP4");
    if (!M.getLive()){H.SetHeader("Accept-Ranges", "bytes, parsec");}

    chromeWorkaround = (req.GetHeader("User-Agent").find("Chrome") != std::string::npos &&
                        req.GetHeader("User-Agent").find("Edge") == std::string::npos &&
                        req.GetHeader("User-Agent").find("OPR/") == std::string::npos);

    uint32_t mainTrack = M.mainTrack();
    if (mainTrack == INVALID_TRACK_ID){
      onFail("No main track found", true);
      return;
    }

    // Check if the url contains .3gp --> if yes, we will send a 3gp header
    sending3GP = (req.url.find(".3gp") != std::string::npos);

    fileSize = 0;
    headerSize = mp4HeaderSize(fileSize, M.getLive());

    seekPoint = Output::startTime();
    // for live we use fragmented mode
    if (M.getLive()){fragSeqNum = 0;}

    sortSet.clear();
    for (std::map<size_t, Comms::Users>::const_iterator subIt = userSelect.begin();
         subIt != userSelect.end(); subIt++){
      keyPart temp;
      temp.trackID = subIt->first;
      DTSC::Keys keys = M.getKeys(subIt->first);
      temp.time = keys.getTime(keys.getFirstValid());
      temp.index = keys.getFirstPart(keys.getFirstValid());
      sortSet.insert(temp);
    }

    byteStart = 0;
    byteEnd = fileSize - 1;
    currPos = 0;
    if (!M.getLive() && req.GetHeader("Range") != ""){
      if (parseRange(req.GetHeader("Range"), byteStart, byteEnd)){findSeekPoint(byteStart, seekPoint, headerSize);}
      if (!byteEnd){
        if (req.GetHeader("Range")[0] == 'p'){
          H.SetBody("Starsystem not in communications range");
          H.SendResponse("416", "Starsystem not in communications range", myConn);
          parseData = false;
          wantRequest = true;
          return;
        }else{
          H.SetBody("Requested Range Not Satisfiable");
          H.SendResponse("416", "Requested Range Not Satisfiable", myConn);
          parseData = false;
          wantRequest = true;
          return;
        }
      }else{
        std::stringstream rangeReply;
        rangeReply << "bytes " << byteStart << "-" << byteEnd << "/" << fileSize;
        H.SetHeader("Content-Length", byteEnd - byteStart + 1);
        H.SetHeader("Content-Range", rangeReply.str());
        H.StartResponse("206", "Partial content", req, myConn);
      }
    }else{
      if (!M.getLive()){H.SetHeader("Content-Length", byteEnd - byteStart + 1);}
      H.StartResponse("200", "OK", req, myConn);
    }

    if (headersOnly){return;}

    //Start sending data
    parseData = true;
    wantRequest = false;
    sentHeader = false;

    //Send MP4 header if needed
    leftOver = byteEnd - byteStart + 1; // add one byte, because range "0-0" = 1 byte of data
    byteEnd++;
    if (byteStart < headerSize){
      // For storing the header.
      if ((!startTime && endTime == 0xffffffffffffffffull) || (endTime == 0)){
        Util::ResizeablePointer headerData;
        if (!mp4Header(headerData, fileSize, M.getLive())){
          FAIL_MSG("Could not generate MP4 header!");
          H.SetBody("Error while generating MP4 header");
          H.SendResponse("500", "Error generating MP4 header", myConn);
          return;
        }
        INFO_MSG("Have %zu bytes, sending %" PRIu64 " bytes", headerData.size(), std::min(headerSize, byteEnd) - byteStart);
        H.Chunkify(headerData + byteStart, std::min(headerSize, byteEnd) - byteStart, myConn);
        leftOver -= std::min(headerSize, byteEnd) - byteStart;
      }
    }
    currPos += headerSize; // we're now guaranteed to be past the header point, no matter what
  }

  void OutMP4::sendNext(){
    //Call parent handler for generic websocket handling
    HTTPOutput::sendNext();
    // Obtain a pointer to the data of this packet
    char *dataPointer = 0;
    size_t len = 0;
    thisPacket.getString("data", dataPointer, len);

    // WebSockets send each packet directly. The packet is constructed in `appendSinglePacketMoof()`. 
    if (webSock) {
      webBuf.truncate(0);
      appendSinglePacketMoof(webBuf);
        
      char mdatHeader[8] ={0x00, 0x00, 0x00, 0x00, 'm', 'd', 'a', 't'};
      Bit::htobl(mdatHeader, 8 + len); /* 8 bytes for the header + length of data. */
      webBuf.append(mdatHeader, 8);
      webBuf.append(dataPointer, len);
      webSock->sendFrame(webBuf, webBuf.size(), 2);

      if (stayLive && thisPacket.getFlag("keyframe")){liveSeek(true);}
      // We must return here, the rest of this function won't work for websockets. 
      return;
    }

    std::string subtitle;

    if (M.getLive()){
      if (nextHeaderTime == 0xffffffffffffffffull){nextHeaderTime = thisPacket.getTime();}

      if (thisPacket.getTime() >= nextHeaderTime){
        if (M.getLive()){
          if (fragSeqNum > 10){
            if (liveSeek()){
              nextHeaderTime = 0xffffffffffffffffull;
              return;
            }
          }

          nextHeaderTime = thisTime + needsLookAhead;
          if (targetParams.count("recstop")){
            uint64_t planStop = atoll(targetParams["recstop"].c_str());
            if (planStop < nextHeaderTime){nextHeaderTime = planStop;}
          }
          if (targetParams.count("stop")){
            uint64_t planStop = atoll(targetParams["stop"].c_str());
            if (planStop < nextHeaderTime){nextHeaderTime = planStop;}
          }
          if (thisTime < nextHeaderTime){sendFragmentHeaderTime(thisTime, nextHeaderTime);}
        }else{
          if (startTime || endTime != 0xffffffffffffffffull){
            sendFragmentHeaderTime(startTime, endTime);
            nextHeaderTime = endTime;
          }else{
            uint32_t mainTrack = M.mainTrack();
            if (mainTrack == INVALID_TRACK_ID){return;}
            sendFragmentHeaderTime(thisPacket.getTime(), 0);
            nextHeaderTime = M.getTimeForFragmentIndex(
                mainTrack, M.getFragmentIndexForTime(mainTrack, thisPacket.getTime()) + 1);
          }
        }
      }

      // generate content in mdat, meaning: send right parts
      DONTEVEN_MSG("Sending tid: %zu size: %zu", thisIdx, len);
      if (webSock) {
        /* create packet */
        webBuf.append(dataPointer, len);
      }else{
        H.Chunkify(dataPointer, len, myConn);
      }
    }
    
    keyPart firstKeyPart = *sortSet.begin();
    DTSC::Parts parts(M.parts(firstKeyPart.trackID));
    /*
    if (thisIdx != firstKeyPart.trackID || thisPacket.getTime() != firstKeyPart.time ||
        len != parts.getSize(firstKeyPart.index)){
      if (thisPacket.getTime() > firstKeyPart.time || thisIdx > firstKeyPart.trackID){
        if (perfect){
          WARN_MSG("Warning: input is inconsistent. Expected %zu:%" PRIu64
                   " (%zub) but got %zu:%" PRIu64 " (%zub) - cancelling playback",
                   firstKeyPart.trackID, firstKeyPart.time, len, thisIdx, thisPacket.getTime(), len);
          perfect = false;
          onFail("Inconsistent input", true);
        }
      }else{
        WARN_MSG("Did not receive expected %zu:%" PRIu64 " (%zub) but got %zu:%" PRIu64
                 " (%zub) - throwing it away",
                 firstKeyPart.trackID, firstKeyPart.time, parts.getSize(firstKeyPart.index),
                 thisIdx, thisPacket.getTime(), len);
        HIGH_MSG("Part %" PRIu64 " in violation", firstKeyPart.index)
      }
      return;
    }
    */

    // The remainder of this function handles non-live situations
    if (M.getLive()){
      if (sortSet.size()){sortSet.erase(sortSet.begin());}
      return;
    }

    // prepend subtitle text with 2 bytes datalength
    if (M.getCodec(firstKeyPart.trackID) == "subtitle"){
      char pre[2];
      Bit::htobs(pre, len);
      subtitle.assign(pre, 2);
      subtitle.append(dataPointer, len);
      dataPointer = (char *)subtitle.c_str();
      len += 2;
    }

    if (isRecording()){
      myConn.SendNow(dataPointer, len);
    }else{
      if (currPos >= byteStart){
        H.Chunkify(dataPointer, std::min(leftOver, (int64_t)len), myConn);

        leftOver -= len;
      }else{
        if (currPos + len > byteStart){
          H.Chunkify(dataPointer + (byteStart - currPos),
                     std::min((uint64_t)leftOver, (len - (byteStart - currPos))), myConn);
          leftOver -= len - (byteStart - currPos);
        }
      }
    }

    // keep track of where we are
    if (!sortSet.empty()){
      keyPart temp = *sortSet.begin();
      sortSet.erase(sortSet.begin());
      currPos += parts.getSize(temp.index);
      if (temp.time + parts.getDuration(temp.index) < M.getLastms(temp.trackID)){// only insert when there are parts left
        temp.time += parts.getDuration(temp.index);
        ++temp.index;
        sortSet.insert(temp);
      }
    }

  }

  void OutMP4::sendHeader(){

    if (webSock) {
      if (!sentHeader){
        sendWebsocketCodecData("codec_data");
      }else{
        sendWebsocketCodecData("tracks");
      }
      JSON::Value r;
      r["type"] = "info";
      r["data"]["msg"] = "Sending header";
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        r["data"]["tracks"].append((uint64_t)it->first);
      }
      webSock->sendFrame(r.toString());

      Util::ResizeablePointer headerData;
      if (!mp4Header(headerData, fileSize, 1)){
        FAIL_MSG("Could not generate MP4 header!");
        return;
      }

      webSock->sendFrame(headerData, headerData.size(), 2);
      sentHeader = true;
      return;
    }

    // If we're doing piped output or output to file, we still need to write the actual header here...
    if (!streamName.size() || isFileTarget()){
      Util::ResizeablePointer headerData;
      if (!mp4Header(headerData, fileSize, M.getLive())){
        FAIL_MSG("Could not generate MP4 header!");
      }else{
        myConn.SendNow(headerData, headerData.size());
      }
      seekPoint = Output::startTime();
    }

        
    vidTrack = getMainSelectedTrack();

    if (M.getLive()){
      bool reSeek = false;
      DTSC::Parts parts(M.parts(vidTrack));
      uint64_t firstPart = parts.getFirstValid();
      uint64_t endPart = parts.getEndValid();
      for (size_t i = firstPart; i < endPart; i++){
        uint32_t pDur = parts.getDuration(i);
        // Make sure we always look ahead at least a single frame
        if (pDur > needsLookAhead){
          needsLookAhead = pDur;
          reSeek = true;
        }
      }
      if (reSeek){
        INFO_MSG("Increased initial lookAhead of %" PRIu64 "ms", needsLookAhead);
        initialSeek();
      }
      timeOffset = currentTime();
    }else{
      seek(seekPoint);
    }
    sentHeader = true;
  }

  void OutMP4::onWebsocketConnect() {
    capa["maxdelay"] = 5000;
    fragSeqNum = 0;
    maxSkipAhead = 0;
    if (M.getLive()){dataWaitTimeout = 450;}
  }

  void OutMP4::onWebsocketFrame() {
    JSON::Value command = JSON::fromString(webSock->data, webSock->data.size());
    if (!command.isMember("type")) {return;}
    
    if (command["type"] == "request_codec_data") {
      //If no supported codecs are passed, assume autodetected capabilities
      if (command.isMember("supported_codecs")) {
        capa.removeMember("exceptions");
        capa["codecs"].null();
        std::set<std::string> dupes;
        jsonForEach(command["supported_codecs"], i){
          if (dupes.count(i->asStringRef())){continue;}
          dupes.insert(i->asStringRef());
          if (supportedVideo.count(i->asStringRef())){
            capa["codecs"][0u][0u].append(i->asStringRef());
          }else if (supportedAudio.count(i->asStringRef())){
            capa["codecs"][0u][1u].append(i->asStringRef());
          }else{
            JSON::Value r;
            r["type"] = "error";
            r["data"] = "Unsupported codec: "+i->asStringRef();
          }
        }
      }
      selectDefaultTracks();
      initialSeek();
      sendHeader();
      return;
    }
  }

  void OutMP4::sendWebsocketCodecData(const std::string& type) {
    JSON::Value r;
    r["type"] = type;
    r["data"]["current"] = currentTime();
    std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin();
    while (it != userSelect.end()) {
      if (prevVidTrack != INVALID_TRACK_ID && M.getType(it->first) == "video" && it->first != prevVidTrack){
        //Skip future tracks
        ++it;
        continue;
      }
      std::string codec = Util::codecString(M.getCodec(it->first), M.getInit(it->first));
      if (!codec.size()) {
        FAIL_MSG("Failed to get the codec string for track: %zu.", it->first);
        ++it;
        continue;
      }
      r["data"]["codecs"].append(codec);
      r["data"]["tracks"].append((uint64_t)it->first);
      ++it;
    }
    webSock->sendFrame(r.toString());
  }
  
  void OutMP4::dropTrack(size_t trackId, const std::string &reason, bool probablyBad){
    if (webSock && (reason == "EOP: data wait timeout" || reason == "disappeared from metadata") && possiblyReselectTracks(currentTime())){
      return;
    }
    return Output::dropTrack(trackId, reason, probablyBad);
  }

}// namespace Mist

