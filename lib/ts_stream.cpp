#include "ts_stream.h"
#include "defines.h"
#include "h264.h"
#include "h265.h"
#include "nal.h"
#include "mp4_generic.h"

namespace TS {
  void Stream::parse(char * newPack, unsigned long long bytePos) {
    Packet newPacket;
    newPacket.FromPointer(newPack);
    parse(newPacket, bytePos);
  }
  
  void Stream::clear(){
    pesStreams.clear();
    pesPositions.clear();
    payloadSize.clear();
    outPackets.clear();
  }
  
  void Stream::parse(Packet & newPack, unsigned long long bytePos) {
    int tid = newPack.getPID();
    if (tid == 0){
      associationTable = newPack;
      pmtTracks.clear();
      int pmtCount = associationTable.getProgramCount();
      for (int i = 0; i < pmtCount; i++){
        pmtTracks.insert(associationTable.getProgramPID(i));
      }
      return;
    }
    //If we are here, the packet is not a PAT.
    //First check if it is listed in the PAT as a PMT track.
    int pmtCount = associationTable.getProgramCount();
    if (pmtTracks.count(tid)){
      mappingTable[tid] = newPack;
      ProgramMappingEntry entry = mappingTable[tid].getEntry(0);
      while (entry){
        unsigned long pid = entry.getElementaryPid();
        switch(entry.getStreamType()){
          case H264:
          case AAC:
          case HEVC:
          case H265:
          case AC3:
            if (!pidToCodec.count(pid)){
              pidToCodec[pid] = entry.getStreamType();
            }
            break;
          default:
            break;
        }
        entry.advance();
      }
      return;
    }
    //If it is not a PMT, check the list of all PMTs to see if this is a new PES track.
    bool inPMT = false;
    for (std::map<unsigned long, ProgramMappingTable>::iterator it = mappingTable.begin(); it!= mappingTable.end(); it++){
      ProgramMappingEntry entry = it->second.getEntry(0); 
      while (entry){
        if (tid == entry.getElementaryPid()){
          inPMT = true;
          break;
        }
        entry.advance();
      }
      if (inPMT){
        break;
      }
    }
    if (!inPMT){
      HIGH_MSG("Encountered a packet on track %d, but the track is not registered in any PMT", tid);
      return;
    }
    pesStreams[tid].push_back(newPack);
    pesPositions[tid].push_back(bytePos);
    if (!newPack.getUnitStart() || pesStreams[tid].size() == 1){
      payloadSize[tid] += newPack.getPayloadLength(); 
    }
    parsePES(tid);
  }

  bool Stream::hasPacketOnEachTrack() const {
    if (!pidToCodec.size()){
      return false;
    }
    if (outPackets.size() != pidToCodec.size()){
      return false;
    }
    for (std::map<unsigned long, unsigned long>::const_iterator it = pidToCodec.begin(); it != pidToCodec.end(); it++){
      if (!outPackets.count(it->first) || !outPackets.at(it->first).size()){
        return false;
      }
    }
    return true;
  }
  
  bool Stream::hasPacket(unsigned long tid) const {
    if (!pesStreams.count(tid)){
      return false;
    }
    if (outPackets.count(tid) && outPackets.at(tid).size()){
      return true;
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
    std::deque<Packet> & inStream = pesStreams[tid];
    std::deque<unsigned long long> & inPositions = pesPositions[tid];
    if (inStream.size() == 1){
      return;
    }
    if (!inStream.back().getUnitStart()){
      return;
    }

    unsigned long long bPos = inPositions.front();
    //Create a buffer for the current PES, and remove it from the pesStreams buffer.
    int paySize = payloadSize[tid];
    char * payload = (char*)malloc(paySize);
    int offset = 0;
    int packNum = inStream.size() - 1;
    std::deque<Packet>::iterator curPack = inStream.begin();
    for (int i = 0; i < packNum; i++){
      memcpy(payload + offset, curPack->getPayload(), curPack->getPayloadLength());
      offset += curPack->getPayloadLength();
      curPack++;
    }
    inStream.erase(inStream.begin(), curPack);
    inPositions.erase(inPositions.begin(), inPositions.begin() + packNum);

    //Parse the PES header
    offset = 0;

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
      }
      if (pidToCodec[tid] == AC3){
        outPackets[tid].push_back(DTSC::Packet());
        outPackets[tid].back().genericFill(timeStamp, timeOffset, tid, pesPayload, realPayloadSize, bPos, 0);
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
                spsInfo[tid] = std::string(parsedData + dataOffset + 4, it->nalSize);
                break;
              }
              case 0x08: {
                ppsInfo[tid] = std::string(parsedData + dataOffset + 4, it->nalSize);
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
                hevcInfo[tid].addUnit(parsedData + dataOffset);
                break;
              }
              default: break;
            }
          }
          dataOffset += 4 + it->nalSize;
        }
        outPackets[tid].push_back(DTSC::Packet());
        outPackets[tid].back().genericFill(timeStamp, timeOffset, tid, parsedData, parsedSize, bPos, isKeyFrame);
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
    payloadSize[tid] = inStream.front().getPayloadLength();
  }

  void Stream::getPacket(unsigned long tid, DTSC::Packet & pack) {
    pack.null();
    if (!hasPacket(tid)){
      ERROR_MSG("Trying to obtain a packet on track %lu, but no full packet is available", tid);
      return;
    }

    pack = outPackets[tid].front();
    outPackets[tid].pop_front();
    
    if (!outPackets[tid].size()){
      outPackets.erase(tid);
    }
  }

  void Stream::getEarliestPacket(DTSC::Packet & pack){
    pack.null();
    if (!hasPacketOnEachTrack()){
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

    getPacket(packTrack, pack);
  }

  void Stream::initializeMetadata(DTSC::Meta & meta) {
    for (std::map<unsigned long, unsigned long>::const_iterator it = pidToCodec.begin(); it != pidToCodec.end(); it++){
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
  }
}
