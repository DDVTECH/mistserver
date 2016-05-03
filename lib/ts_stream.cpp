#include "ts_stream.h"
#include "defines.h"
#include "h264.h"
#include "h265.h"
#include "nal.h"
#include "mp4_generic.h"
#include <sys/stat.h>

namespace TS {
  Stream::Stream(bool _threaded){
    threaded = _threaded;
    if (threaded){
      globalSem.open("MstTSInputLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
      if (!globalSem) {
        FAIL_MSG("Creating semaphore failed: %s", strerror(errno));
        threaded = false;
        DEBUG_MSG(DLVL_FAIL, "Creating semaphore failed: %s", strerror(errno));
        return;
      }
    }
  }

  Stream::~Stream(){
    if (threaded){
      globalSem.unlink();
    }
  }

  void Stream::parse(char * newPack, unsigned long long bytePos) {
    Packet newPacket;
    newPacket.FromPointer(newPack);
    parse(newPacket, bytePos);
  }

  void Stream::clear(){
    if (threaded){
      globalSem.wait();
    }
    pesStreams.clear();
    pesPositions.clear();
    outPackets.clear();
    if (threaded){
      globalSem.post();
    }
  }

  void Stream::add(char * newPack, unsigned long long bytePos) {
    Packet newPacket;
    newPacket.FromPointer(newPack);
    add(newPacket, bytePos);
  }

  void Stream::add(Packet & newPack, unsigned long long bytePos) {
    if (threaded){
      globalSem.wait();
    }

    int tid = newPack.getPID();
    pesStreams[tid].push_back(newPack);
    pesPositions[tid].push_back(bytePos);

    if (threaded){
      globalSem.post();
    }
  }

  bool Stream::isDataTrack(unsigned long tid){
    if (tid == 0){
      return false;
    }
    if (threaded){
      globalSem.wait();
    }
    bool result = !pmtTracks.count(tid);
    if (threaded){
      globalSem.post();
    }
    return result;
  }
  
  void Stream::parse(unsigned long tid) {
    if (threaded){
      globalSem.wait();
    }
    if (!pesStreams.count(tid) || pesStreams[tid].size() == 0){
      if (threaded){
        globalSem.post();
      }
      return;
    }
    std::deque<Packet> & trackPackets = pesStreams[tid];

    if (threaded){
      globalSem.post();
    }
    
    //Handle PAT packets
    if (tid == 0){
      ///\todo Keep track of updates in PAT instead of keeping only the last PAT as a reference
      
      if (threaded){
        globalSem.wait();
      }
      associationTable = trackPackets.back();
      lastPAT = Util::bootSecs();
      if (threaded){
        globalSem.post();
      }


      int pmtCount = associationTable.getProgramCount();
      for (int i = 0; i < pmtCount; i++){
        pmtTracks.insert(associationTable.getProgramPID(i));
      }

      if (threaded){
        globalSem.wait();
      }
      pesStreams.erase(0);
      pesPositions.erase(0);
      if (threaded){
        globalSem.post();
      }
      return;
    }

    //Handle PMT packets
    if (pmtTracks.count(tid)){
      ///\todo Keep track of updates in PMT instead of keeping only the last PMT per program as a reference
      if (threaded){
        globalSem.wait();
      }
      mappingTable[tid] = trackPackets.back();
      lastPMT[tid] = Util::bootSecs();
      if (threaded){
        globalSem.post();
      }
      ProgramMappingEntry entry = mappingTable[tid].getEntry(0);
      while (entry){
        unsigned long pid = entry.getElementaryPid();
        unsigned long sType = entry.getStreamType();
        switch(sType){
          case H264:
          case AAC:
          case HEVC:
          case H265:
          case AC3:
          case ID3:
            pidToCodec[pid] = sType;
            if (sType == ID3){
              metaInit[pid] = std::string(entry.getESInfo(), entry.getESInfoLength());
            }
            break;
          default:
            break;
        }
        entry.advance();
      }

      if (threaded){
        globalSem.wait();
      }
      pesStreams.erase(tid);
      pesPositions.erase(tid);
      if (threaded){
        globalSem.post();
      }
      
      return;
    }

    if (threaded){
      globalSem.wait();
    }

    bool parsePes = false;

    int packNum = 1;
    std::deque<Packet> & inStream = pesStreams[tid];
    std::deque<Packet>::iterator curPack = inStream.begin();
    curPack++;
    while (curPack != inStream.end() && !curPack->getUnitStart()){
      curPack++;
      packNum++;
    }
    if (curPack != inStream.end()){
      parsePes = true;
    }

    if (threaded){
      globalSem.post();
    }

    if (parsePes){
      parsePES(tid);
    }
  }

  void Stream::parse(Packet & newPack, unsigned long long bytePos) {
    add(newPack, bytePos);

    int tid = newPack.getPID();
    parse(tid);
  }

  bool Stream::hasPacketOnEachTrack() const {
    if (threaded){
      globalSem.wait();
    }
    if (!pidToCodec.size() || pidToCodec.size() != outPackets.size()){
      if (threaded){
        globalSem.post();
      }
      return false;
    }
    for (std::map<unsigned long, unsigned long>::const_iterator it = pidToCodec.begin(); it != pidToCodec.end(); it++){
      if (!hasPacket(it->first)){
        if (threaded){
          globalSem.post();
        }
        return false;
      }
    }
    if (threaded){
      globalSem.post();
    }
    return true;
  }
  
  bool Stream::hasPacket(unsigned long tid) const {
    if (threaded){
      globalSem.wait();
    }
    if (!pesStreams.count(tid)){
      if (threaded){
        globalSem.post();
      }
      return false;
    }
    if (outPackets.count(tid) && outPackets.at(tid).size()){
      if (threaded){
        globalSem.post();
      }
      return true;
    }
    std::deque<Packet>::const_iterator curPack = pesStreams.at(tid).begin();
    curPack++;
    while (curPack != pesStreams.at(tid).end() && !curPack->getUnitStart()){
      curPack++;
    }
    if (curPack != pesStreams.at(tid).end()){
      if (threaded){
        globalSem.post();
      }
      return true;
    }
    if (threaded){
      globalSem.post();
    }
    return false;
  }

  unsigned long long decodePTS(const char * data){
    unsigned long long time;
    time = ((data[0] >> 1) & 0x07);
    time <<= 15;
    time |= ((int)data[1] << 7) | ((data[2] >> 1) & 0x7F);
    time <<= 15;
    time |= ((int)data[3] << 7) | ((data[4] >> 1) & 0x7F);
    time /= 90;
    return time;
  }

  void Stream::parsePES(unsigned long tid){
    if (threaded){
      globalSem.wait();
    }
    std::deque<Packet> & inStream = pesStreams[tid];
    std::deque<unsigned long long> & inPositions = pesPositions[tid];
    if (inStream.size() == 1){
      if (threaded){
        globalSem.post();
      }
      return;
    }
    //Find number of packets before unit Start
    int packNum = 1;

    std::deque<Packet>::iterator curPack = inStream.begin();
    curPack++;
    while (curPack != inStream.end() && !curPack->getUnitStart()){
      curPack++;
      packNum++;
    }
    if (curPack == inStream.end()){
      if (threaded){
        globalSem.post();
      }
      return;
    }

    unsigned long long bPos = inPositions.front();
    //Create a buffer for the current PES, and remove it from the pesStreams buffer.
    int  paySize = 0;
    
    curPack = inStream.begin();
    for (int i = 0; i < packNum; i++){
      paySize += curPack->getPayloadLength();
      curPack++;
    }
    char * payload = (char*)malloc(paySize);
    paySize = 0;
    curPack = inStream.begin();
    int lastCtr = curPack->getContinuityCounter() - 1;
    for (int i = 0; i < packNum; i++){
      if (curPack->getContinuityCounter() - lastCtr != 1 && curPack->getContinuityCounter()){
        INFO_MSG("Parsing a pes on track %d, missed %d packets", tid, curPack->getContinuityCounter() - lastCtr - 1);
      }
      lastCtr = curPack->getContinuityCounter();
      memcpy(payload + paySize, curPack->getPayload(), curPack->getPayloadLength());
      paySize += curPack->getPayloadLength();
      curPack++;
    }
    inStream.erase(inStream.begin(), curPack);
    inPositions.erase(inPositions.begin(), inPositions.begin() + packNum);
    if (threaded){
      globalSem.post();
    }

    //Parse the PES header
    int offset = 0;

    while(offset < paySize){
      const char * pesHeader = payload + offset;

      //Check for large enough buffer
      if ((paySize - offset) < 9 || (paySize - offset) < 9 + pesHeader[8]){
        INFO_MSG("Not enough data on track %lu (%d / %d), discarding remainder of data", tid, paySize - offset, 9 + pesHeader[8]);
        break;
      }

      //Check for valid PES lead-in
      if(pesHeader[0] != 0 || pesHeader[1] != 0x00 || pesHeader[2] != 0x01){
        INFO_MSG("Invalid PES Lead in on track %lu, discarding it", tid);
        break;
      }

      //Read the payload size.
      //Note: if the payload size is 0, then we assume the pes packet will cover the entire TS Unit.
      //Note: this is technically only allowed for video pes streams.
      unsigned long long realPayloadSize = (((int)pesHeader[4] << 8) | pesHeader[5]);
      if (!realPayloadSize){
        realPayloadSize = paySize;
      }
      if (pidToCodec[tid] == AAC || pidToCodec[tid] == MP3 || pidToCodec[tid] == AC3){
        realPayloadSize -= (3 + pesHeader[8]);
      }else{
        realPayloadSize -= (9 + pesHeader[8]);
      }
      

      //Read the metadata for this PES Packet
      ///\todo Determine keyframe-ness
      unsigned int timeStamp = 0;
      unsigned int timeOffset = 0;
      unsigned int pesOffset = 9;
      if ((pesHeader[7] >> 6) & 0x02){//Check for PTS presence
        timeStamp = decodePTS(pesHeader + pesOffset);
        pesOffset += 5;
        if (((pesHeader[7] & 0xC0) >> 6) & 0x01){//Check for DTS presence (yes, only if PTS present)
          timeOffset = timeStamp;
          timeStamp = decodePTS(pesHeader + pesOffset);
          pesOffset += 5;
          timeOffset -= timeStamp;
        }
      }

      if (paySize - offset - pesOffset < realPayloadSize){
        INFO_MSG("Not enough data left on track %lu.", tid);
        break;
      }

      char * pesPayload = payload + offset + pesOffset;

      //Create a new (empty) DTSC Packet at the end of the buffer
      if (pidToCodec[tid] == AAC){
        //Parse all the ADTS packets
        unsigned long offsetInPes = 0;
        unsigned long samplesRead = 0;
        if (threaded){
          globalSem.wait();
        }
        while (offsetInPes < realPayloadSize){
          outPackets[tid].push_back(DTSC::Packet());
          aac::adts adtsPack(pesPayload + offsetInPes, realPayloadSize - offsetInPes);
          if (!adtsInfo.count(tid)){
            adtsInfo[tid] = adtsPack;
          }
          outPackets[tid].back().genericFill(timeStamp + ((samplesRead * 1000) / adtsPack.getFrequency()), timeOffset, tid, adtsPack.getPayload(), adtsPack.getPayloadSize(), bPos, 0);
          samplesRead += adtsPack.getSampleCount();
          offsetInPes += adtsPack.getHeaderSize() + adtsPack.getPayloadSize();
        }
        if (threaded){
          globalSem.post();
        }
      }
      if (pidToCodec[tid] == ID3 || pidToCodec[tid] == AC3){
        if (threaded){
          globalSem.wait();
        }
        outPackets[tid].push_back(DTSC::Packet());
        outPackets[tid].back().genericFill(timeStamp, timeOffset, tid, pesPayload, realPayloadSize, bPos, 0);
        if (threaded){
          globalSem.post();
        }
      }
      if (pidToCodec[tid] == H264 || pidToCodec[tid] == HEVC || pidToCodec[tid] == H265){
        //Convert from annex b
        char * parsedData = (char*)malloc(realPayloadSize * 2);
        bool isKeyFrame = false;
        unsigned long parsedSize = nalu::fromAnnexB(pesPayload, realPayloadSize, parsedData);
        std::deque<nalu::nalData> nalInfo;
        if (pidToCodec[tid] == H264) {
          nalInfo = h264::analysePackets(parsedData, parsedSize);
        }
        if (pidToCodec[tid] == HEVC || pidToCodec[tid] == H265){
          nalInfo = h265::analysePackets(parsedData, parsedSize);
        }
        int dataOffset = 0;
        for (std::deque<nalu::nalData>::iterator it = nalInfo.begin(); it != nalInfo.end(); it++){
          if (pidToCodec[tid] == H264){
            switch (it->nalType){
              case 0x05: {
                isKeyFrame = true; 
                break;
              }
              case 0x07: {
                if (threaded){
                  globalSem.wait();
                }
                spsInfo[tid] = std::string(parsedData + dataOffset + 4, it->nalSize);
                if (threaded){
                  globalSem.post();
                }
                break;
              }
              case 0x08: {
                if (threaded){
                  globalSem.wait();
                }
                ppsInfo[tid] = std::string(parsedData + dataOffset + 4, it->nalSize);
                if (threaded){
                  globalSem.post();
                }
                break;
              }
              default: break;
            }
          }
          if (pidToCodec[tid] == HEVC || pidToCodec[tid] == H265){
            switch (it->nalType){
              case 2: case 3: //TSA Picture
              case 4: case 5: //STSA Picture
              case 6: case 7: //RADL Picture
              case 8: case 9: //RASL Picture
              case 16: case 17: case 18: //BLA Picture
              case 19: case 20: //IDR Picture
              case 21: { //CRA Picture
                isKeyFrame = true; 
                break;
              }
              case 32:
              case 33:
              case 34: {
                if (threaded){
                  globalSem.wait();
                }
                hevcInfo[tid].addUnit(parsedData + dataOffset);
                if (threaded){
                  globalSem.post();
                }
                break;
              }
              default: break;
            }
          }
          dataOffset += 4 + it->nalSize;
        }
        if (threaded){
          globalSem.wait();
        }
        outPackets[tid].push_back(DTSC::Packet());
        outPackets[tid].back().genericFill(timeStamp, timeOffset, tid, parsedData, parsedSize, bPos, isKeyFrame);
        if (threaded){
          globalSem.post();
        }
        free(parsedData);
      }
      //We are done with the realpayload size, reverse calculation so we know the correct offset increase.
      if (pidToCodec[tid] == AAC){
        realPayloadSize += (3 + pesHeader[8]);
      }else{
        realPayloadSize += (9 + pesHeader[8]);
      }
      offset += realPayloadSize + 6;
    }
    free(payload);
  }

  void Stream::getPacket(unsigned long tid, DTSC::Packet & pack) {
    pack.null();
    if (!hasPacket(tid)){
      ERROR_MSG("Trying to obtain a packet on track %lu, but no full packet is available", tid);
      return;
    }

    if (threaded){
      globalSem.wait();
    }
    bool packetReady = outPackets.count(tid) && outPackets[tid].size();
    if (threaded){
      globalSem.post();
    }

    if (!packetReady){
      parse(tid);
    }
    
    if (threaded){
      globalSem.wait();
    }
    packetReady = outPackets.count(tid) && outPackets[tid].size();
    if (threaded){
      globalSem.post();
    }
    
    if (!packetReady){
      ERROR_MSG("Obtaining a packet on track %lu failed", tid);
      return;
    }

    if (threaded){
      globalSem.wait();
    }
    pack = outPackets[tid].front();
    outPackets[tid].pop_front();
    
    if (!outPackets[tid].size()){
      outPackets.erase(tid);
    }

    if (threaded){
      globalSem.post();
    }
  }

  void Stream::getEarliestPacket(DTSC::Packet & pack){
    if (threaded){
      globalSem.wait();
    }
    pack.null();
    if (!hasPacketOnEachTrack()){
      if (threaded){
        globalSem.post();
      }
      return;
    }

    unsigned long packTime = 0xFFFFFFFFull;
    unsigned long packTrack = 0;

    for (std::map<unsigned long, std::deque<DTSC::Packet> >::iterator it = outPackets.begin(); it != outPackets.end(); it++){
      if (it->second.front().getTime() < packTime){
        packTrack = it->first;
        packTime = it->second.front().getTime();
      }
    }
    if (threaded){
      globalSem.post();
    }

    getPacket(packTrack, pack);
  }

  void Stream::initializeMetadata(DTSC::Meta & meta, unsigned long tid) {
    if (threaded){
      globalSem.wait();
    }
    for (std::map<unsigned long, unsigned long>::const_iterator it = pidToCodec.begin(); it != pidToCodec.end(); it++){
      if (tid && it->first != tid){
        continue;
      }
      if (!meta.tracks.count(it->first) && it->second == H264){
        if (!spsInfo.count(it->first) || !ppsInfo.count(it->first)){
          continue;
        }
        meta.tracks[it->first].type = "video";
        meta.tracks[it->first].codec = "H264";
        meta.tracks[it->first].trackID = it->first;
        std::string tmpBuffer = spsInfo[it->first];
        h264::sequenceParameterSet sps(spsInfo[it->first].data(), spsInfo[it->first].size());
        h264::SPSMeta spsChar = sps.getCharacteristics();
        meta.tracks[it->first].width = spsChar.width;
        meta.tracks[it->first].height = spsChar.height;
        meta.tracks[it->first].fpks = spsChar.fps * 1000;
        MP4::AVCC avccBox;
        avccBox.setVersion(1);
        avccBox.setProfile(spsInfo[it->first][1]);
        avccBox.setCompatibleProfiles(spsInfo[it->first][2]);
        avccBox.setLevel(spsInfo[it->first][3]);
        avccBox.setSPSNumber(1);
        avccBox.setSPS(spsInfo[it->first]);
        avccBox.setPPSNumber(1);
        avccBox.setPPS(ppsInfo[it->first]);
        meta.tracks[it->first].init = std::string(avccBox.payload(), avccBox.payloadSize());
      }
      if (!meta.tracks.count(it->first) && (it->second == HEVC || it->second == H265)){
        if (!hevcInfo.count(it->first) || !hevcInfo[it->first].haveRequired()){
          continue;
        }
        meta.tracks[it->first].type = "video";
        meta.tracks[it->first].codec = "HEVC";
        meta.tracks[it->first].trackID = it->first;
        meta.tracks[it->first].init = hevcInfo[it->first].generateHVCC();
      }
      if (!meta.tracks.count(it->first) && it->second == ID3){
        meta.tracks[it->first].type = "meta";
        meta.tracks[it->first].codec = "ID3";
        meta.tracks[it->first].trackID = it->first;
        meta.tracks[it->first].init = metaInit[it->first];
      }
      if (!meta.tracks.count(it->first) && it->second == AC3){
        meta.tracks[it->first].type = "audio";
        meta.tracks[it->first].codec = "AC3";
        meta.tracks[it->first].trackID = it->first;
        meta.tracks[it->first].size = 16;
        ///\todo Fix these 2 values
        meta.tracks[it->first].rate = 0;
        meta.tracks[it->first].channels = 0;
      }
      if (!meta.tracks.count(it->first) && it->second == AAC){
        meta.tracks[it->first].type = "audio";
        meta.tracks[it->first].codec = "AAC";
        meta.tracks[it->first].trackID = it->first;
        meta.tracks[it->first].size = 16;
        meta.tracks[it->first].rate = adtsInfo[it->first].getFrequency();
        meta.tracks[it->first].channels = adtsInfo[it->first].getChannelCount();
        char audioInit[2];//5 bits object type, 4 bits frequency index, 4 bits channel index
        audioInit[0] = ((adtsInfo[it->first].getAACProfile() & 0x1F) << 3) | ((adtsInfo[it->first].getFrequencyIndex() & 0x0E) >> 1);
        audioInit[1] = ((adtsInfo[it->first].getFrequencyIndex() & 0x01) << 7) | ((adtsInfo[it->first].getChannelConfig() & 0x0F) << 3);
        meta.tracks[it->first].init = std::string(audioInit, 2);
      }
    }
    if (threaded){
      globalSem.post();
    }
  }

  std::set<unsigned long> Stream::getActiveTracks() {
    if (threaded){
      globalSem.wait();
    }
    std::set<unsigned long> result;
    //Track 0 is always active
    result.insert(0);
    //IF PAT updated in the last 5 seconds, check for contents
    if (Util::bootSecs() - lastPAT < 5){
      int pmtCount = associationTable.getProgramCount();
      //For each PMT
      for (int i = 0; i < pmtCount; i++){
        int pid = associationTable.getProgramPID(i);
        //Add PMT track
        result.insert(pid);
        //IF PMT updated in last 5 seconds, check for contents
        if (Util::bootSecs() - lastPMT[pid] < 5){
          ProgramMappingEntry entry = mappingTable[pid].getEntry(0);
          //Add all tracks in PMT
          while (entry){
            switch(entry.getStreamType()){
              case H264:
              case AAC:
              case HEVC:
              case H265:
              case AC3:
              case ID3:
                result.insert(entry.getElementaryPid());
                break;
              default:
                break;
            }
            entry.advance();
          }
        }
      }
    }
    if (threaded){
      globalSem.post();
    }
    return result;
  }

  void Stream::eraseTrack(unsigned long tid){
    if (threaded){
      globalSem.wait();
    }
    pesStreams.erase(tid);
    pesPositions.erase(tid);
    outPackets.erase(tid);
    if (threaded){
      globalSem.post();
    }
  }
}
