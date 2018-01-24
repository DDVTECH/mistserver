#include "output_ebml.h"
#include <mist/ebml_socketglue.h>
#include <mist/riff.h>

namespace Mist{
  OutEBML::OutEBML(Socket::Connection &conn) : HTTPOutput(conn){
    currentClusterTime = 0;
    newClusterTime = 0;
    segmentSize = 0xFFFFFFFFFFFFFFFFull;
    tracksSize = 0;
    infoSize = 0;
    cuesSize = 0;
    seekheadSize = 0;
    doctype = "matroska";
  }

  void OutEBML::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "EBML";
    capa["desc"] = "Enables MKV and WebM streaming over HTTP.";
    capa["url_rel"] = "/$.webm";
    capa["url_match"].append("/$.mkv");
    capa["url_match"].append("/$.webm");
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("VP8");
    capa["codecs"][0u][0u].append("VP9");
    capa["codecs"][0u][0u].append("theora");
    capa["codecs"][0u][0u].append("MPEG2");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("vorbis");
    capa["codecs"][0u][1u].append("opus");
    capa["codecs"][0u][1u].append("PCM");
    capa["codecs"][0u][1u].append("ALAW");
    capa["codecs"][0u][1u].append("ULAW");
    capa["codecs"][0u][1u].append("MP2");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("FLOAT");
    capa["codecs"][0u][1u].append("AC3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/webm";
    capa["methods"][0u]["priority"] = 8ll;
  }

  /// Calculates the size of a Cluster (contents only) and returns it.
  /// Bases the calculation on the currently selected tracks and the given start/end time for the cluster.
  uint32_t OutEBML::clusterSize(uint64_t start, uint64_t end){
    uint32_t sendLen = EBML::sizeElemUInt(EBML::EID_TIMECODE, start);
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin();
         it != selectedTracks.end(); it++){
      DTSC::Track &thisTrack = myMeta.tracks[*it];
      uint32_t firstPart = 0;
      unsigned long long int prevParts = 0;
      uint64_t curMS = 0;
      for (std::deque<DTSC::Key>::iterator it2 = thisTrack.keys.begin();
           it2 != thisTrack.keys.end(); it2++){
        if (it2->getTime() > start && it2 != thisTrack.keys.begin()){break;}
        firstPart += prevParts;
        prevParts = it2->getParts();
        curMS = it2->getTime();
      }
      size_t maxParts = thisTrack.parts.size();
      for (size_t i = firstPart; i < maxParts; i++){
        if (curMS >= end){break;}
        if (curMS >= start){
          uint32_t blkLen = EBML::sizeSimpleBlock(thisTrack.trackID, thisTrack.parts[i].getSize());
          sendLen += blkLen;
        }
        curMS += thisTrack.parts[i].getDuration();
      }
    }
    return sendLen;
  }

  void OutEBML::sendNext(){
    if (thisPacket.getTime() >= newClusterTime){
      currentClusterTime = thisPacket.getTime();
      if (myMeta.vod){
        //In case of VoD, clusters are aligned with the main track fragments
        DTSC::Track &Trk = myMeta.tracks[getMainSelectedTrack()];
        uint32_t fragIndice = Trk.timeToFragnum(currentClusterTime);
        newClusterTime = Trk.getKey(Trk.fragments[fragIndice].getNumber()).getTime() + Trk.fragments[fragIndice].getDuration();
        //The last fragment should run until the end of time
        if (fragIndice == Trk.fragments.size() - 1){
          newClusterTime = 0xFFFFFFFFFFFFFFFFull;
        }
        EXTREME_MSG("Cluster: %llu - %llu (%lu/%lu) = %llu", currentClusterTime, newClusterTime, fragIndice, Trk.fragments.size(), clusterSize(currentClusterTime, newClusterTime));
      }else{
        //In live, clusters are aligned with the lookAhead time
        newClusterTime += needsLookAhead;
      }
      EBML::sendElemHead(myConn, EBML::EID_CLUSTER, clusterSize(currentClusterTime, newClusterTime));
      EBML::sendElemUInt(myConn, EBML::EID_TIMECODE, currentClusterTime);
    }

    EBML::sendSimpleBlock(myConn, thisPacket, currentClusterTime,
                          myMeta.tracks[thisPacket.getTrackId()].type != "video");
  }

  std::string OutEBML::trackCodecID(const DTSC::Track &Trk){
    if (Trk.codec == "opus"){return "A_OPUS";}
    if (Trk.codec == "H264"){return "V_MPEG4/ISO/AVC";}
    if (Trk.codec == "HEVC"){return "V_MPEGH/ISO/HEVC";}
    if (Trk.codec == "VP8"){return "V_VP8";}
    if (Trk.codec == "VP9"){return "V_VP9";}
    if (Trk.codec == "AAC"){return "A_AAC";}
    if (Trk.codec == "vorbis"){return "A_VORBIS";}
    if (Trk.codec == "theora"){return "V_THEORA";}
    if (Trk.codec == "MPEG2"){return "V_MPEG2";}
    if (Trk.codec == "PCM"){return "A_PCM/INT/BIG";}
    if (Trk.codec == "MP2"){return "A_MPEG/L2";}
    if (Trk.codec == "MP3"){return "A_MPEG/L3";}
    if (Trk.codec == "AC3"){return "A_AC3";}
    if (Trk.codec == "ALAW"){return "A_MS/ACM";}
    if (Trk.codec == "ULAW"){return "A_MS/ACM";}
    if (Trk.codec == "FLOAT"){return "A_PCM/FLOAT/IEEE";}
    return "E_UNKNOWN";
  }

  void OutEBML::sendElemTrackEntry(const DTSC::Track &Trk){
    // First calculate the sizes of the TrackEntry and Audio/Video elements.
    uint32_t sendLen = 0;
    uint32_t subLen = 0;
    sendLen += EBML::sizeElemUInt(EBML::EID_TRACKNUMBER, Trk.trackID);
    sendLen += EBML::sizeElemUInt(EBML::EID_TRACKUID, Trk.trackID);
    sendLen += EBML::sizeElemStr(EBML::EID_CODECID, trackCodecID(Trk));
    sendLen += EBML::sizeElemStr(EBML::EID_LANGUAGE, Trk.lang.size() ? Trk.lang : "und");
    sendLen += EBML::sizeElemUInt(EBML::EID_FLAGLACING, 0);
    if (Trk.codec == "ALAW" || Trk.codec == "ULAW"){
      sendLen += EBML::sizeElemStr(EBML::EID_CODECPRIVATE, std::string((size_t)18, '\000'));
    }else{
      if (Trk.init.size()){sendLen += EBML::sizeElemStr(EBML::EID_CODECPRIVATE, Trk.init);}
    }
    if (Trk.type == "video"){
      sendLen += EBML::sizeElemUInt(EBML::EID_TRACKTYPE, 1);
      subLen += EBML::sizeElemUInt(EBML::EID_PIXELWIDTH, Trk.width);
      subLen += EBML::sizeElemUInt(EBML::EID_PIXELHEIGHT, Trk.height);
      sendLen += EBML::sizeElemHead(EBML::EID_VIDEO, subLen);
    }
    if (Trk.type == "audio"){
      sendLen += EBML::sizeElemUInt(EBML::EID_TRACKTYPE, 2);
      subLen += EBML::sizeElemUInt(EBML::EID_CHANNELS, Trk.channels);
      subLen += EBML::sizeElemDbl(EBML::EID_SAMPLINGFREQUENCY, Trk.rate);
      subLen += EBML::sizeElemUInt(EBML::EID_BITDEPTH, Trk.size);
      sendLen += EBML::sizeElemHead(EBML::EID_AUDIO, subLen);
    }
    sendLen += subLen;

    // Now actually send.
    EBML::sendElemHead(myConn, EBML::EID_TRACKENTRY, sendLen);
    EBML::sendElemUInt(myConn, EBML::EID_TRACKNUMBER, Trk.trackID);
    EBML::sendElemUInt(myConn, EBML::EID_TRACKUID, Trk.trackID);
    EBML::sendElemStr(myConn, EBML::EID_CODECID, trackCodecID(Trk));
    EBML::sendElemStr(myConn, EBML::EID_LANGUAGE, Trk.lang.size() ? Trk.lang : "und");
    EBML::sendElemUInt(myConn, EBML::EID_FLAGLACING, 0);
    if (Trk.codec == "ALAW" || Trk.codec == "ULAW"){
      std::string init =
          RIFF::fmt::generate(((Trk.codec == "ALAW") ? 6 : 7), Trk.channels, Trk.rate, Trk.bps,
                              Trk.channels * (Trk.size << 3), Trk.size);
      EBML::sendElemStr(myConn, EBML::EID_CODECPRIVATE, init.substr(8));
    }else{
      if (Trk.init.size()){EBML::sendElemStr(myConn, EBML::EID_CODECPRIVATE, Trk.init);}
    }
    if (Trk.type == "video"){
      EBML::sendElemUInt(myConn, EBML::EID_TRACKTYPE, 1);
      EBML::sendElemHead(myConn, EBML::EID_VIDEO, subLen);
      EBML::sendElemUInt(myConn, EBML::EID_PIXELWIDTH, Trk.width);
      EBML::sendElemUInt(myConn, EBML::EID_PIXELHEIGHT, Trk.height);
    }
    if (Trk.type == "audio"){
      EBML::sendElemUInt(myConn, EBML::EID_TRACKTYPE, 2);
      EBML::sendElemHead(myConn, EBML::EID_AUDIO, subLen);
      EBML::sendElemUInt(myConn, EBML::EID_CHANNELS, Trk.channels);
      EBML::sendElemDbl(myConn, EBML::EID_SAMPLINGFREQUENCY, Trk.rate);
      EBML::sendElemUInt(myConn, EBML::EID_BITDEPTH, Trk.size);
    }
  }

  uint32_t OutEBML::sizeElemTrackEntry(const DTSC::Track &Trk){
    // Calculate the sizes of the TrackEntry and Audio/Video elements.
    uint32_t sendLen = 0;
    uint32_t subLen = 0;
    sendLen += EBML::sizeElemUInt(EBML::EID_TRACKNUMBER, Trk.trackID);
    sendLen += EBML::sizeElemUInt(EBML::EID_TRACKUID, Trk.trackID);
    sendLen += EBML::sizeElemStr(EBML::EID_CODECID, trackCodecID(Trk));
    sendLen += EBML::sizeElemStr(EBML::EID_LANGUAGE, Trk.lang.size() ? Trk.lang : "und");
    sendLen += EBML::sizeElemUInt(EBML::EID_FLAGLACING, 0);
    if (Trk.codec == "ALAW" || Trk.codec == "ULAW"){
      sendLen += EBML::sizeElemStr(EBML::EID_CODECPRIVATE, std::string((size_t)18, '\000'));
    }else{
      if (Trk.init.size()){sendLen += EBML::sizeElemStr(EBML::EID_CODECPRIVATE, Trk.init);}
    }
    if (Trk.type == "video"){
      sendLen += EBML::sizeElemUInt(EBML::EID_TRACKTYPE, 1);
      subLen += EBML::sizeElemUInt(EBML::EID_PIXELWIDTH, Trk.width);
      subLen += EBML::sizeElemUInt(EBML::EID_PIXELHEIGHT, Trk.height);
      sendLen += EBML::sizeElemHead(EBML::EID_VIDEO, subLen);
    }
    if (Trk.type == "audio"){
      sendLen += EBML::sizeElemUInt(EBML::EID_TRACKTYPE, 2);
      subLen += EBML::sizeElemUInt(EBML::EID_CHANNELS, Trk.channels);
      subLen += EBML::sizeElemDbl(EBML::EID_SAMPLINGFREQUENCY, Trk.rate);
      subLen += EBML::sizeElemUInt(EBML::EID_BITDEPTH, Trk.size);
      sendLen += EBML::sizeElemHead(EBML::EID_AUDIO, subLen);
    }
    sendLen += subLen;
    return EBML::sizeElemHead(EBML::EID_TRACKENTRY, sendLen) + sendLen;
  }

  void OutEBML::sendHeader(){
    double duration = 0;
    DTSC::Track &Trk = myMeta.tracks[getMainSelectedTrack()];
    if (myMeta.vod){
      duration = Trk.lastms - Trk.firstms;
    }
    if (myMeta.live){
      needsLookAhead = 420;
    }
    //EBML header and Segment
    EBML::sendElemEBML(myConn, doctype);
    EBML::sendElemHead(myConn, EBML::EID_SEGMENT, segmentSize); // Default = Unknown size
    if (myMeta.vod){
      //SeekHead
      EBML::sendElemHead(myConn, EBML::EID_SEEKHEAD, seekSize);
      EBML::sendElemSeek(myConn, EBML::EID_INFO, seekheadSize);
      EBML::sendElemSeek(myConn, EBML::EID_TRACKS, seekheadSize + infoSize);
      EBML::sendElemSeek(myConn, EBML::EID_CUES, seekheadSize + infoSize + tracksSize);
    }
    //Info
    EBML::sendElemInfo(myConn, "MistServer " PACKAGE_VERSION, duration);
    //Tracks
    uint32_t trackSizes = 0;
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin();
         it != selectedTracks.end(); it++){
      trackSizes += sizeElemTrackEntry(myMeta.tracks[*it]);
    }
    EBML::sendElemHead(myConn, EBML::EID_TRACKS, trackSizes);
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin();
         it != selectedTracks.end(); it++){
      sendElemTrackEntry(myMeta.tracks[*it]);
    }
    if (myMeta.vod){
      EBML::sendElemHead(myConn, EBML::EID_CUES, cuesSize);
      uint64_t tmpsegSize = infoSize + tracksSize + seekheadSize + cuesSize + EBML::sizeElemHead(EBML::EID_CUES, cuesSize);
      uint32_t fragNo = 0;
      for (std::deque<DTSC::Fragment>::iterator it = Trk.fragments.begin(); it != Trk.fragments.end(); ++it){
        uint64_t clusterStart = Trk.getKey(it->getNumber()).getTime();
        //The first fragment always starts at time 0, even if the main track does not.
        if (!fragNo){clusterStart = 0;}
        EBML::sendElemCuePoint(myConn, clusterStart, Trk.trackID, tmpsegSize, 0);
        tmpsegSize += clusterSizes[fragNo];
        ++fragNo;
      }
    }
    sentHeader = true;
  }

  /// Seeks to the given byte position by doing a regular seek and remembering the byte offset from that point
  void OutEBML::byteSeek(uint64_t startPos){
    INFO_MSG("Seeking to %llu bytes", startPos);
    sentHeader = false;
    newClusterTime = 0;
    if (startPos == 0){
      seek(0);
      return;
    }
    uint64_t headerSize = EBML::sizeElemEBML(doctype) + EBML::sizeElemHead(EBML::EID_SEGMENT, segmentSize) + seekheadSize + infoSize + tracksSize + EBML::sizeElemHead(EBML::EID_CUES, cuesSize) + cuesSize;
    if (startPos < headerSize){
      HIGH_MSG("Seek went into or before header");
      seek(0);
      myConn.skipBytes(startPos);
      return;
    }
    startPos -= headerSize;
    sentHeader = true;//skip the header
    DTSC::Track &Trk = myMeta.tracks[getMainSelectedTrack()];
    for (std::map<uint64_t, uint64_t>::iterator it = clusterSizes.begin(); it != clusterSizes.end(); ++it){
      VERYHIGH_MSG("Cluster %llu (%llu bytes) -> %llu to go", it->first, it->second, startPos);
      if (startPos < it->second){
        HIGH_MSG("Seek to fragment %llu (%llu ms)", it->first, Trk.getKey(Trk.fragments[it->first].getNumber()).getTime());
        myConn.skipBytes(startPos);
        seek(Trk.getKey(Trk.fragments[it->first].getNumber()).getTime());
        newClusterTime = Trk.getKey(Trk.fragments[it->first].getNumber()).getTime();
        return;
      }
      startPos -= it->second;
    }
    //End of file. This probably won't work right, but who cares, it's the end of the file.
  }

  void OutEBML::onHTTP(){
    std::string method = H.method;
    if(method == "OPTIONS" || method == "HEAD"){
      H.Clean();
      H.setCORSHeaders();
      H.SetHeader("Content-Type", "video/MP4");
      H.SetHeader("Accept-Ranges", "bytes, parsec");
      H.SendResponse("200", "OK", myConn);
      return;
    }
    if (H.url.find(".webm") != std::string::npos){
      doctype = "webm";
    }else{
      doctype = "matroska";
    }

    //Calculate the sizes of various parts, if we're VoD.
    uint64_t totalSize = 0;
    if (myMeta.vod){
      calcVodSizes();
      //We now know the full size of the segment, thus can calculate the total size
      totalSize = EBML::sizeElemEBML(doctype) + EBML::sizeElemHead(EBML::EID_SEGMENT, segmentSize) + segmentSize;
    }

    uint64_t byteEnd = totalSize-1;
    uint64_t byteStart = 0;
    
    /*LTS-START*/
    // allow setting of max lead time through buffer variable.
    // max lead time is set in MS, but the variable is in integer seconds for simplicity.
    if (H.GetVar("buffer") != ""){maxSkipAhead = JSON::Value(H.GetVar("buffer")).asInt() * 1000;}
    //allow setting of play back rate through buffer variable.
    //play back rate is set in MS per second, but the variable is a simple multiplier.
    if (H.GetVar("rate") != ""){
      long long int multiplier = JSON::Value(H.GetVar("rate")).asInt();
      if (multiplier){
        realTime = 1000 / multiplier;
      }else{
        realTime = 0;
      }
    }
    if (H.GetHeader("X-Mist-Rate") != ""){
      long long int multiplier = JSON::Value(H.GetHeader("X-Mist-Rate")).asInt();
      if (multiplier){
        realTime = 1000 / multiplier;
      }else{
        realTime = 0;
      }
    }
    /*LTS-END*/

    char rangeType = ' ';
    if (!myMeta.live){
      if (H.GetHeader("Range") != ""){
        if (parseRange(byteStart, byteEnd)){
          if (H.GetVar("buffer") == ""){
            DTSC::Track &Trk = myMeta.tracks[getMainSelectedTrack()];
            maxSkipAhead = (Trk.lastms - Trk.firstms) / 20 + 7500;
          }
        }
        rangeType = H.GetHeader("Range")[0];
      }
    }
    H.Clean(); //make sure no parts of old requests are left in any buffers
    H.setCORSHeaders();
    H.SetHeader("Content-Type", "video/webm");
    if (myMeta.vod){
      H.SetHeader("Accept-Ranges", "bytes, parsec");
    }
    if (rangeType != ' '){
      if (!byteEnd){
        if (rangeType == 'p'){
          H.SetBody("Starsystem not in communications range");
          H.SendResponse("416", "Starsystem not in communications range", myConn);
          return;
        }else{
          H.SetBody("Requested Range Not Satisfiable");
          H.SendResponse("416", "Requested Range Not Satisfiable", myConn);
          return;
        }
      }else{
        std::stringstream rangeReply;
        rangeReply << "bytes " << byteStart << "-" << byteEnd << "/" << totalSize;
        H.SetHeader("Content-Length", byteEnd - byteStart + 1);
        H.SetHeader("Content-Range", rangeReply.str());
        /// \todo Switch to chunked?
        H.SendResponse("206", "Partial content", myConn);
        //H.StartResponse("206", "Partial content", HTTP_R, conn);
        byteSeek(byteStart);
      }
    }else{
      if (myMeta.vod){
        H.SetHeader("Content-Length", byteEnd - byteStart + 1);
      }
      /// \todo Switch to chunked?
      H.SendResponse("200", "OK", myConn);
      //HTTP_S.StartResponse(HTTP_R, conn);
    }
    parseData = true;
    wantRequest = false;
  }

  void OutEBML::calcVodSizes(){
    if (segmentSize != 0xFFFFFFFFFFFFFFFFull){
      //Already calculated
      return;
    }
    DTSC::Track &Trk = myMeta.tracks[getMainSelectedTrack()];
    double duration = Trk.lastms - Trk.firstms;
    //Calculate the segment size
    //Segment contains SeekHead, Info, Tracks, Cues (in that order)
    //Howeveer, SeekHead is dependent on Info/Tracks sizes, so we calculate those first.
    //Calculating Info size
    infoSize = EBML::sizeElemInfo("MistServer " PACKAGE_VERSION, duration);
    //Calculating Tracks size
    tracksSize = 0;
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin();
         it != selectedTracks.end(); it++){
      tracksSize += sizeElemTrackEntry(myMeta.tracks[*it]);
    }
    tracksSize += EBML::sizeElemHead(EBML::EID_TRACKS, tracksSize);
    //Calculating SeekHead size
    //Positions are relative to the first Segment, byte 0 = first byte of contents of Segment.
    //Tricky starts here: the size of the SeekHead element is dependent on the seek offsets contained inside,
    //which are in turn dependent on the size of the SeekHead element. Fun times! We loop until it stabilizes.
    uint32_t oldseekSize = 0;
    do {
      oldseekSize = seekSize;
      seekSize = EBML::sizeElemSeek(EBML::EID_INFO, seekheadSize) +
                 EBML::sizeElemSeek(EBML::EID_TRACKS, seekheadSize + infoSize) + 
                 EBML::sizeElemSeek(EBML::EID_CUES, seekheadSize + infoSize + tracksSize);
      seekheadSize = EBML::sizeElemHead(EBML::EID_SEEKHEAD, seekSize) + seekSize;
    }while(seekSize != oldseekSize);
    //The Cues are tricky: the Cluster offsets are dependent on the size of Cues itself.
    //Which, in turn, is dependent on the Cluster offsets.
    //We make this a bit easier by pre-calculating the sizes of all clusters first
    uint64_t fragNo = 0;
    for (std::deque<DTSC::Fragment>::iterator it = Trk.fragments.begin(); it != Trk.fragments.end(); ++it){
      uint64_t clusterStart = Trk.getKey(it->getNumber()).getTime();
      uint64_t clusterEnd = clusterStart + it->getDuration();
      //The first fragment always starts at time 0, even if the main track does not.
      if (!fragNo){clusterStart = 0;}
      //The last fragment always ends at the end, even if the main track does not.
      if (fragNo == Trk.fragments.size() - 1){clusterEnd = 0xFFFFFFFFFFFFFFFFull;}
      uint64_t cSize = clusterSize(clusterStart, clusterEnd);
      clusterSizes[fragNo] = cSize + EBML::sizeElemHead(EBML::EID_CLUSTER, cSize);
      ++fragNo;
    }
    //Calculating Cues size
    //We also calculate Clusters here: Clusters are grouped by fragments of the main track.
    //CueClusterPosition uses the same offsets as SeekPosition.
    //CueRelativePosition is the offset from that Cluster's first content byte.
    //All this uses the same technique as above. More fun times!
    uint32_t oldcuesSize = 0;
    do {
      oldcuesSize = cuesSize;
      segmentSize = infoSize + tracksSize + seekheadSize + cuesSize + EBML::sizeElemHead(EBML::EID_CUES, cuesSize);
      uint32_t cuesInside = 0;
      fragNo = 0;
      for (std::deque<DTSC::Fragment>::iterator it = Trk.fragments.begin(); it != Trk.fragments.end(); ++it){
        uint64_t clusterStart = Trk.getKey(it->getNumber()).getTime();
        //The first fragment always starts at time 0, even if the main track does not.
        if (!fragNo){clusterStart = 0;}
        cuesInside += EBML::sizeElemCuePoint(clusterStart, Trk.trackID, segmentSize, 0);
        segmentSize += clusterSizes[fragNo];
        ++fragNo;
      }
      cuesSize = cuesInside;
    }while(cuesSize != oldcuesSize);
  }

}// namespace Mist

