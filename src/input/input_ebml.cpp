#include "input_ebml.h"
#include <mist/defines.h>
#include <mist/ebml.h>
#include <mist/bitfields.h>

namespace Mist{

  uint16_t maxEBMLFrameOffset = 0;
  bool frameOffsetKnown = false;

  InputEBML::InputEBML(Util::Config *cfg) : Input(cfg){
    timeScale = 1.0;
    capa["name"] = "EBML";
    capa["desc"] = "Allows loading MKV, MKA, MK3D, MKS and WebM files for Video on Demand, or accepts live streams in those formats over standard input.";
    capa["source_match"].append("/*.mkv");
    capa["source_match"].append("/*.mka");
    capa["source_match"].append("/*.mk3d");
    capa["source_match"].append("/*.mks");
    capa["source_match"].append("/*.webm");
    capa["priority"] = 9ll;
    capa["codecs"].append("H264");
    capa["codecs"].append("HEVC");
    capa["codecs"].append("VP8");
    capa["codecs"].append("VP9");
    capa["codecs"].append("AV1");
    capa["codecs"].append("opus");
    capa["codecs"].append("vorbis");
    capa["codecs"].append("theora");
    capa["codecs"].append("AAC");
    capa["codecs"].append("PCM");
    capa["codecs"].append("ALAW");
    capa["codecs"].append("ULAW");
    capa["codecs"].append("MP2");
    capa["codecs"].append("MPEG2");
    capa["codecs"].append("MP3");
    capa["codecs"].append("AC3");
    capa["codecs"].append("FLOAT");
    capa["codecs"].append("DTS");
    capa["codecs"].append("JSON");
    capa["codecs"].append("subtitle");
    lastClusterBPos = 0;
    lastClusterTime = 0;
    bufferedPacks = 0;
    wantBlocks = true;
  }

  std::string ASStoSRT(const char * ptr, uint32_t len){
    uint16_t commas = 0;
    uint16_t brackets = 0;
    std::string tmpStr;
    tmpStr.reserve(len);
    for (uint32_t i = 0; i < len; ++i){
      //Skip everything until the 8th comma
      if (commas < 8){
        if (ptr[i] == ','){commas++;}
        continue;
      }
      if (ptr[i] == '{'){brackets++; continue;}
      if (ptr[i] == '}'){brackets--; continue;}
      if (!brackets){
        if (ptr[i] == '\\' && i < len-1 && (ptr[i+1] == 'N' || ptr[i+1] == 'n')){
          tmpStr += '\n';
          ++i;
          continue;
        }
        tmpStr += ptr[i];
      }
    }
    return tmpStr;
  }


  bool InputEBML::checkArguments(){
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
    }
    return true;
  }

  bool InputEBML::needsLock() {
    //Standard input requires no lock, everything else does.
    if (config->getString("input") != "-"){
      return true;
    }else{
      return false;
    }
  }

  bool InputEBML::preRun(){
    if (config->getString("input") == "-"){
      inFile = stdin;
    }else{
      // open File
      inFile = fopen(config->getString("input").c_str(), "r");
      if (!inFile){return false;}
    }
    return true;
  }

  bool InputEBML::readElement(){
    ptr.size() = 0;
    readingMinimal = true;
    uint32_t needed = EBML::Element::needBytes(ptr, ptr.size(), readingMinimal);
    while (ptr.size() < needed){
      if (!ptr.allocate(needed)){return false;}
      if (!fread(ptr + ptr.size(), needed - ptr.size(), 1, inFile)){
        //We assume if there is no current data buffered, that we are at EOF and don't print a warning
        if (ptr.size()){
          FAIL_MSG("Could not read more data! (have %lu, need %lu)", ptr.size(), needed);
        }
        return false;
      }
      ptr.size() = needed;
      needed = EBML::Element::needBytes(ptr, ptr.size(), readingMinimal);
      if (ptr.size() >= needed){
        // Make sure TrackEntry types are read whole
        if (readingMinimal && EBML::Element(ptr).getID() == EBML::EID_TRACKENTRY){
          readingMinimal = false;
          needed = EBML::Element::needBytes(ptr, ptr.size(), readingMinimal);
        }
      }
    }
    EBML::Element E(ptr);
    if (E.getID() == EBML::EID_CLUSTER){
      if (inFile == stdin){
        lastClusterBPos = 0;
      }else{
        int64_t bp = Util::ftell(inFile);
        if(bp == -1 && errno == ESPIPE){
          lastClusterBPos = 0;
        }else{
          lastClusterBPos = bp;
        }
      }
      DONTEVEN_MSG("Found a cluster at position %llu", lastClusterBPos);
    }
    if (E.getID() == EBML::EID_TIMECODE){
      lastClusterTime = E.getValUInt();
      DONTEVEN_MSG("Cluster time %llu ms", lastClusterTime);
    }
    return true;
  }

  bool InputEBML::readExistingHeader(){
    if (!Input::readExistingHeader()){return false;}
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin();
         it != myMeta.tracks.end(); ++it){
      if (it->second.codec == "PCMLE"){
        it->second.codec = "PCM";
        swapEndianness.insert(it->first);
      }
    }
    if (myMeta.inputLocalVars.isMember("timescale")){
        timeScale = ((double)myMeta.inputLocalVars["timescale"].asInt()) / 1000000.0;
    }
    if (myMeta.inputLocalVars.isMember("maxframeoffset")){
      maxEBMLFrameOffset = myMeta.inputLocalVars["maxframeoffset"].asInt();
      frameOffsetKnown = true;
    }
    return true;
  }

  bool InputEBML::readHeader(){
    if (!inFile){return false;}
    // Create header file from file
    uint64_t bench = Util::getMicros();

    while (readElement()){
      EBML::Element E(ptr, readingMinimal);
      if (E.getID() == EBML::EID_TRACKENTRY){
        EBML::Element tmpElem = E.findChild(EBML::EID_TRACKNUMBER);
        if (!tmpElem){
          ERROR_MSG("Track without track number encountered, ignoring");
          continue;
        }
        uint64_t trackNo = tmpElem.getValUInt();
        tmpElem = E.findChild(EBML::EID_CODECID);
        if (!tmpElem){
          ERROR_MSG("Track without codec id encountered, ignoring");
          continue;
        }
        std::string codec = tmpElem.getValString(), trueCodec, trueType, lang, init;
        if (codec == "V_MPEG4/ISO/AVC"){
          trueCodec = "H264";
          trueType = "video";
          tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
          if (tmpElem){init = tmpElem.getValStringUntrimmed();}
        }
        if (codec == "V_MPEGH/ISO/HEVC"){
          trueCodec = "HEVC";
          trueType = "video";
          tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
          if (tmpElem){init = tmpElem.getValStringUntrimmed();}
        }
        if (codec == "V_AV1"){
          trueCodec = "AV1";
          trueType = "video";
        }
        if (codec == "V_VP9"){
          trueCodec = "VP9";
          trueType = "video";
        }
        if (codec == "V_VP8"){
          trueCodec = "VP8";
          trueType = "video";
        }
        if (codec == "A_OPUS"){
          trueCodec = "opus";
          trueType = "audio";
          tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
          if (tmpElem){init = tmpElem.getValStringUntrimmed();}
        }
        if (codec == "A_VORBIS"){
          trueCodec = "vorbis";
          trueType = "audio";
          tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
          if (tmpElem){init = tmpElem.getValStringUntrimmed();}
        }
        if (codec == "V_THEORA"){
          trueCodec = "theora";
          trueType = "video";
          tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
          if (tmpElem){init = tmpElem.getValStringUntrimmed();}
        }
        if (codec == "A_AAC"){
          trueCodec = "AAC";
          trueType = "audio";
          tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
          if (tmpElem){init = tmpElem.getValStringUntrimmed();}
        }
        if (codec == "A_DTS"){
          trueCodec = "DTS";
          trueType = "audio";
        }
        if (codec == "A_PCM/INT/BIG"){
          trueCodec = "PCM";
          trueType = "audio";
        }
        if (codec == "A_PCM/INT/LIT"){
          trueCodec = "PCMLE";
          trueType = "audio";
        }
        if (codec == "A_AC3"){
          trueCodec = "AC3";
          trueType = "audio";
        }
        if (codec == "A_MPEG/L3"){
          trueCodec = "MP3";
          trueType = "audio";
        }
        if (codec == "A_MPEG/L2"){
          trueCodec = "MP2";
          trueType = "audio";
        }
        if (codec == "V_MPEG2"){
          trueCodec = "MPEG2";
          trueType = "video";
        }
        if (codec == "A_PCM/FLOAT/IEEE"){
          trueCodec = "FLOAT";
          trueType = "audio";
        }
        if (codec == "M_JSON"){
          trueCodec = "JSON";
          trueType = "meta";
        }
        if (codec == "S_TEXT/UTF8"){
          trueCodec = "subtitle";
          trueType = "meta";
        }
        if (codec == "S_TEXT/ASS" || codec == "S_TEXT/SSA"){
          trueCodec = "subtitle";
          trueType = "meta";
          tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
          if (tmpElem){init = tmpElem.getValStringUntrimmed();}
        }
        if (codec == "A_MS/ACM"){
          tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
          if (tmpElem){
            std::string WAVEFORMATEX = tmpElem.getValStringUntrimmed();
            unsigned int formatTag = Bit::btohs_le(WAVEFORMATEX.data());
            switch (formatTag){
              case 3:
                trueCodec = "FLOAT";
                trueType = "audio";
                break;
              case 6:
                trueCodec = "ALAW";
                trueType = "audio";
                break;
              case 7:
                trueCodec = "ULAW";
                trueType = "audio";
                break;
              case 85:
                trueCodec = "MP3";
                trueType = "audio";
                break;
              default:
                ERROR_MSG("Unimplemented A_MS/ACM formatTag: %u", formatTag);
                break;
            }
          }
        }
        if (!trueCodec.size()){
          WARN_MSG("Unrecognised codec id %s ignoring", codec.c_str());
          continue;
        }
        tmpElem = E.findChild(EBML::EID_LANGUAGE);
        if (tmpElem){lang = tmpElem.getValString();}
        DTSC::Track &Trk = myMeta.tracks[trackNo];
        Trk.trackID = trackNo;
        Trk.lang = lang;
        Trk.codec = trueCodec;
        Trk.type = trueType;
        Trk.init = init;
        if (Trk.type == "video"){
          tmpElem = E.findChild(EBML::EID_PIXELWIDTH);
          Trk.width = tmpElem ? tmpElem.getValUInt() : 0;
          tmpElem = E.findChild(EBML::EID_PIXELHEIGHT);
          Trk.height = tmpElem ? tmpElem.getValUInt() : 0;
          Trk.fpks = 0;
        }
        if (Trk.type == "audio"){
          tmpElem = E.findChild(EBML::EID_CHANNELS);
          Trk.channels = tmpElem ? tmpElem.getValUInt() : 1;
          tmpElem = E.findChild(EBML::EID_BITDEPTH);
          Trk.size = tmpElem ? tmpElem.getValUInt() : 0;
          tmpElem = E.findChild(EBML::EID_SAMPLINGFREQUENCY);
          Trk.rate = tmpElem ? (int)tmpElem.getValFloat() : 8000;
        }
        INFO_MSG("Detected track: %s", Trk.getIdentifier().c_str());
      }
      if (E.getID() == EBML::EID_TIMECODESCALE){
        uint64_t timeScaleVal = E.getValUInt();
        myMeta.inputLocalVars["timescale"] = (long long)timeScaleVal;
        timeScale = ((double)timeScaleVal) / 1000000.0;
      }
      //Live streams stop parsing the header as soon as the first Cluster is encountered
      if (E.getID() == EBML::EID_CLUSTER && !needsLock()){return true;}
      if (E.getType() == EBML::ELEM_BLOCK){
        EBML::Block B(ptr);
        uint64_t tNum = B.getTrackNum();
        uint64_t newTime = lastClusterTime + B.getTimecode();
        trackPredictor &TP = packBuf[tNum];
        DTSC::Track &Trk = myMeta.tracks[tNum];
        bool isVideo = (Trk.type == "video");
        bool isAudio = (Trk.type == "audio");
        bool isASS = (Trk.codec == "subtitle" && Trk.init.size());
        //If this is a new video keyframe, flush the corresponding trackPredictor
        if (isVideo && B.isKeyframe()){
          while (TP.hasPackets(true)){
            packetData &C = TP.getPacketData(true);
            myMeta.update(C.time, C.offset, C.track, C.dsize, C.bpos, C.key);
            TP.remove();
          }
          TP.flush();
        }
        for (uint64_t frameNo = 0; frameNo < B.getFrameCount(); ++frameNo){
          if (frameNo){
            if (Trk.codec == "AAC"){
              newTime += (1000000 / Trk.rate)/timeScale;//assume ~1000 samples per frame
            } else if (Trk.codec == "MP3"){
              newTime += (1152000 / Trk.rate)/timeScale;//1152 samples per frame
            } else if (Trk.codec == "DTS"){
              //Assume 512 samples per frame (DVD default)
              //actual amount can be calculated from data, but data
              //is not available during header generation...
              //See: http://www.stnsoft.com/DVD/dtshdr.html
              newTime += (512000 / Trk.rate)/timeScale;
            }else{
              newTime += 1/timeScale;
              ERROR_MSG("Unknown frame duration for codec %s - timestamps WILL be wrong!", Trk.codec.c_str());
            }
          }
          uint32_t frameSize = B.getFrameSize(frameNo);
          if (isASS){
            char * ptr = (char *)B.getFrameData(frameNo);
            std::string assStr = ASStoSRT(ptr, frameSize);
            frameSize = assStr.size();
          }
          if (frameSize){
            TP.add(newTime*timeScale, 0, tNum, frameSize, lastClusterBPos,
                 B.isKeyframe() && !isAudio, isVideo);
          }
        }
        while (TP.hasPackets() && (isVideo || frameOffsetKnown)){
          frameOffsetKnown = true;
          packetData &C = TP.getPacketData(isVideo);
          myMeta.update(C.time, C.offset, C.track, C.dsize, C.bpos, C.key);
          TP.remove();
        }
      }
    }

    if (packBuf.size()){
      for (std::map<uint64_t, trackPredictor>::iterator it = packBuf.begin(); it != packBuf.end();
           ++it){
        trackPredictor &TP = it->second;
        while (TP.hasPackets(true)){
          packetData &C = TP.getPacketData(myMeta.tracks[it->first].type == "video");
          myMeta.update(C.time, C.offset, C.track, C.dsize, C.bpos, C.key);
          TP.remove();
        }
      }
    }

    myMeta.inputLocalVars["maxframeoffset"] = (long long)maxEBMLFrameOffset;

    bench = Util::getMicros(bench);
    INFO_MSG("Header generated in %llu ms", bench / 1000);
    packBuf.clear();
    bufferedPacks = 0;
    myMeta.toFile(config->getString("input") + ".dtsh");
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin();
         it != myMeta.tracks.end(); ++it){
      if (it->second.codec == "PCMLE"){
        it->second.codec = "PCM";
        swapEndianness.insert(it->first);
      }
    }
    return true;
  }

  void InputEBML::fillPacket(packetData &C){
    if (swapEndianness.count(C.track)){
      switch (myMeta.tracks[C.track].size){
      case 16:{
        char *ptr = C.ptr;
        uint32_t ptrSize = C.dsize;
        for (uint32_t i = 0; i < ptrSize; i += 2){
          char tmpchar = ptr[i];
          ptr[i] = ptr[i + 1];
          ptr[i + 1] = tmpchar;
        }
      }break;
      case 24:{
        char *ptr = C.ptr;
        uint32_t ptrSize = C.dsize;
        for (uint32_t i = 0; i < ptrSize; i += 3){
          char tmpchar = ptr[i];
          ptr[i] = ptr[i + 2];
          ptr[i + 2] = tmpchar;
        }
      }break;
      case 32:{
        char *ptr = C.ptr;
        uint32_t ptrSize = C.dsize;
        for (uint32_t i = 0; i < ptrSize; i += 4){
          char tmpchar = ptr[i];
          ptr[i] = ptr[i + 3];
          ptr[i + 3] = tmpchar;
          tmpchar = ptr[i + 1];
          ptr[i + 1] = ptr[i + 2];
          ptr[i + 2] = tmpchar;
        }
      }break;
      }
    }
    thisPacket.genericFill(C.time, C.offset, C.track, C.ptr, C.dsize, C.bpos, C.key);
  }

  void InputEBML::getNext(bool smart){
    // Make sure we empty our buffer first
    if (bufferedPacks && packBuf.size()){
      for (std::map<uint64_t, trackPredictor>::iterator it = packBuf.begin();
           it != packBuf.end(); ++it){
        trackPredictor &TP = it->second;
        if (TP.hasPackets()){
          packetData &C = TP.getPacketData(myMeta.tracks[it->first].type == "video");
          fillPacket(C);
          TP.remove();
          --bufferedPacks;
          return;
        }
      }
    }
    EBML::Block B;
    if (wantBlocks){
      do{
        if (!readElement()){
          // Make sure we empty our buffer first
          if (bufferedPacks && packBuf.size()){
            for (std::map<uint64_t, trackPredictor>::iterator it = packBuf.begin();
                 it != packBuf.end(); ++it){
              trackPredictor &TP = it->second;
              if (TP.hasPackets(true)){
                packetData &C = TP.getPacketData(myMeta.tracks[it->first].type == "video");
                fillPacket(C);
                TP.remove();
                --bufferedPacks;
                return;
              }
            }
          }
          // No more buffer? Set to empty
          thisPacket.null();
          return;
        }
        B = EBML::Block(ptr);
      }while (!B || B.getType() != EBML::ELEM_BLOCK || !selectedTracks.count(B.getTrackNum()));
    }else{
      B = EBML::Block(ptr);
    }

    uint64_t tNum = B.getTrackNum();
    uint64_t newTime = lastClusterTime + B.getTimecode();
    trackPredictor &TP = packBuf[tNum];
    DTSC::Track & Trk = myMeta.tracks[tNum];
    bool isVideo = (Trk.type == "video");
    bool isAudio = (Trk.type == "audio");
    bool isASS = (Trk.codec == "subtitle" && Trk.init.size());

    //If this is a new video keyframe, flush the corresponding trackPredictor
    if (isVideo && B.isKeyframe() && bufferedPacks){
      if (TP.hasPackets(true)){
        wantBlocks = false;
        packetData &C = TP.getPacketData(true);
        fillPacket(C);
        TP.remove();
        --bufferedPacks;
        return;
      }
    }
    if (isVideo && B.isKeyframe()){
      TP.flush();
    }
    wantBlocks = true;


    for (uint64_t frameNo = 0; frameNo < B.getFrameCount(); ++frameNo){
      if (frameNo){
        if (Trk.codec == "AAC"){
          newTime += (1000000 / Trk.rate)/timeScale;//assume ~1000 samples per frame
        } else if (Trk.codec == "MP3"){
          newTime += (1152000 / Trk.rate)/timeScale;//1152 samples per frame
        } else if (Trk.codec == "DTS"){
          //Assume 512 samples per frame (DVD default)
          //actual amount can be calculated from data, but data
          //is not available during header generation...
          //See: http://www.stnsoft.com/DVD/dtshdr.html
          newTime += (512000 / Trk.rate)/timeScale;
        }else{
          ERROR_MSG("Unknown frame duration for codec %s - timestamps WILL be wrong!", Trk.codec.c_str());
        }
      }
      uint32_t frameSize = B.getFrameSize(frameNo);
      if (frameSize){
        char * ptr = (char *)B.getFrameData(frameNo);
        if (isASS){
          std::string assStr = ASStoSRT(ptr, frameSize);
          frameSize = assStr.size();
          memcpy(ptr, assStr.data(), frameSize);
        }
        if (frameSize){
          TP.add(newTime*timeScale, 0, tNum, frameSize, lastClusterBPos,
          B.isKeyframe() && !isAudio, isVideo, (void *)ptr);
          ++bufferedPacks;
        }
      }
    }
    if (TP.hasPackets()){
      packetData &C = TP.getPacketData(isVideo);
      fillPacket(C);
      TP.remove();
      --bufferedPacks;
    }else{
      // We didn't set thisPacket yet. Read another.
      // Recursing is fine, this can only happen a few times in a row.
      getNext(smart);
    }
  }

  void InputEBML::seek(int seekTime){
    wantBlocks = true;
    packBuf.clear();
    bufferedPacks = 0;
    uint64_t mainTrack = getMainSelectedTrack();
    DTSC::Track Trk = myMeta.tracks[mainTrack];
    bool isVideo = (Trk.type == "video");
    uint64_t seekPos = Trk.keys[0].getBpos();
    // Replay the parts of the previous keyframe, so the timestaps match up
    uint64_t partCount = 0;
    for (unsigned int i = 0; i < Trk.keys.size(); i++){
      if (Trk.keys[i].getTime() > seekTime){
        break;
      }
      partCount += Trk.keys[i].getParts();
      DONTEVEN_MSG("Seeking to %lu, found %llu...", seekTime, Trk.keys[i].getTime());
      seekPos = Trk.keys[i].getBpos();
    }
    Util::fseek(inFile, seekPos, SEEK_SET);
  }

}// namespace Mist

