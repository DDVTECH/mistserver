#include "ts_stream.h"
#include "defines.h"
#include "h264.h"
#include "h265.h"
#include "mp4_generic.h"
#include "nal.h"
#include <sys/stat.h>
#include <stdint.h>
#include "mpeg.h"


namespace TS{

  void ADTSRemainder::setRemainder(const aac::adts &p, const void *source, const uint32_t avail,
                                   const uint64_t bPos){
    if (!p.getCompleteSize()){return;}

    if (max < p.getCompleteSize()){
      void *newmainder = realloc(data, p.getCompleteSize());
      if (newmainder){
        max = p.getCompleteSize();
        data = (char *)newmainder;
      }
    }
    if (max >= p.getCompleteSize()){
      len = p.getCompleteSize();
      now = avail;
      bpos = bPos;
      memcpy(data, source, now);
    }
  }

  void ADTSRemainder::append(const char *p, uint32_t pLen){
    if (now + pLen > len){
      FAIL_MSG("Data to append does not fit into the remainder");
      return;
    }

    memcpy(data + now, p, pLen);
    now += pLen;
  }

  bool ADTSRemainder::isComplete(){return (len == now);}

  void ADTSRemainder::clear(){
    len = 0;
    now = 0;
    bpos = 0;
  }

  ADTSRemainder::ADTSRemainder(){
    data = 0;
    max = 0;
    now = 0;
    len = 0;
    bpos = 0;
  }
  ADTSRemainder::~ADTSRemainder(){
    if (data){
      free(data);
      data = 0;
    }
  }

  uint32_t ADTSRemainder::getLength(){return len;}

  uint64_t ADTSRemainder::getBpos(){return bpos;}

  uint32_t ADTSRemainder::getTodo(){return len - now;}
  char *ADTSRemainder::getData(){return data;}

  Stream::Stream(bool _threaded){
    threaded = _threaded;
  }

  Stream::~Stream(){
  }

  void Stream::parse(char *newPack, unsigned long long bytePos){
    Packet newPacket;
    newPacket.FromPointer(newPack);
    parse(newPacket, bytePos);
  }

  void Stream::partialClear(){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    pesStreams.clear();
    pesPositions.clear();
    outPackets.clear();
    buildPacket.clear();
    seenUnitStart.clear();
    lastms.clear();
    rolloverCount.clear();
  }

  void Stream::clear(){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    partialClear();
    pidToCodec.clear();
    adtsInfo.clear();
    spsInfo.clear();
    ppsInfo.clear();
    hevcInfo.clear();
    metaInit.clear();
    descriptors.clear();
    mappingTable.clear();
    lastPMT.clear();
    lastPAT = 0;
    pmtTracks.clear();
    remainders.clear();
    associationTable = ProgramAssociationTable();
  }

  void Stream::finish(){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    if (!pesStreams.size()){return;}

    for (std::map<unsigned long, std::deque<Packet> >::const_iterator i = pesStreams.begin();
         i != pesStreams.end(); i++){
      parsePES(i->first, true);
    }
  }

  void Stream::add(char *newPack, unsigned long long bytePos){
    Packet newPacket;
    newPacket.FromPointer(newPack);
    add(newPacket, bytePos);
  }

  void Stream::add(Packet &newPack, unsigned long long bytePos){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    int tid = newPack.getPID();
    bool unitStart = newPack.getUnitStart();
    std::deque<Packet> & PS = pesStreams[tid];
    if ((pidToCodec.count(tid) || tid == 0 || newPack.isPMT()) &&
        (unitStart || PS.size())){
      PS.push_back(newPack);
      if (unitStart){
        pesPositions[tid].push_back(bytePos);
        ++(seenUnitStart[tid]);
      }
    }
  }

  bool Stream::isDataTrack(unsigned long tid){
    if (tid == 0){return false;}
    {
      tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
      return !pmtTracks.count(tid);
    }
  }

  void Stream::parse(unsigned long tid){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    if (!pesStreams.count(tid) || pesStreams[tid].size() == 0){
      return;
    }
    std::deque<Packet> &trackPackets = pesStreams[tid];

    // Handle PAT packets
    if (tid == 0){
      ///\todo Keep track of updates in PAT instead of keeping only the last PAT as a reference

      associationTable = trackPackets.back();
      associationTable.parsePIDs();
      lastPAT = Util::bootSecs();

      int pmtCount = associationTable.getProgramCount();
      for (int i = 0; i < pmtCount; i++){pmtTracks.insert(associationTable.getProgramPID(i));}

      pesStreams.erase(0);
      return;
    }

    // Ignore conditional access packets. We don't care.
    if (tid == 1){return;}

    // Handle PMT packets
    if (pmtTracks.count(tid)){
      ///\todo Keep track of updates in PMT instead of keeping only the last PMT per program as a
      /// reference
      mappingTable[tid] = trackPackets.back();
      lastPMT[tid] = Util::bootSecs();
      ProgramMappingEntry entry = mappingTable[tid].getEntry(0);
      while (entry){
        unsigned long pid = entry.getElementaryPid();
        unsigned long sType = entry.getStreamType();
        switch (sType){
        case H264:
        case AAC:
        case H265:
        case AC3:
        case ID3:
        case MP2:
        case MPEG2:
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

      pesStreams.erase(tid);
      return;
    }

    if(seenUnitStart[tid] > 1) {
      parsePES(tid);
    }
  }

  void Stream::parse(Packet &newPack, unsigned long long bytePos){
    add(newPack, bytePos);
    if (newPack.getUnitStart()){
      parse(newPack.getPID());
    }
  }

  bool Stream::hasPacketOnEachTrack() const{
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    if (!pidToCodec.size()){
      // INFO_MSG("no packet on each track 1, pidtocodec.size: %d, outpacket.size: %d",
      // pidToCodec.size(), outPackets.size());
      return false;
    }

    unsigned int missing = 0;
    uint64_t firstTime = 0xffffffffffffffffull, lastTime = 0;
    for (std::map<unsigned long, unsigned long>::const_iterator it = pidToCodec.begin();
         it != pidToCodec.end(); it++){
      if (!hasPacket(it->first)){
        missing++;
      }else{
        if (outPackets.at(it->first).front().getTime() < firstTime){
          firstTime = outPackets.at(it->first).front().getTime();
        }
        if (outPackets.at(it->first).back().getTime() > lastTime){
          lastTime = outPackets.at(it->first).back().getTime();
        }
      }
    }

    return (!missing || (missing != pidToCodec.size() && lastTime - firstTime > 2000));
  }

  bool Stream::hasPacket(unsigned long tid) const{
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    std::map<unsigned long, std::deque<Packet> >::const_iterator pesIt = pesStreams.find(tid);
    if (pesIt == pesStreams.end()){
      return false;
    }
    if (outPackets.count(tid) && outPackets.at(tid).size()){
      return true;
    }
    if (pidToCodec.count(tid) && seenUnitStart.count(tid) && seenUnitStart.at(tid) > 1){
      return true;
    }
    return false;
  }

  bool Stream::hasPacket() const{
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    if (!pesStreams.size()){
      return false;
    }

    if (outPackets.size()){
      for (std::map<unsigned long, std::deque<DTSC::Packet> >::const_iterator i =
               outPackets.begin();
           i != outPackets.end(); i++){
        if (i->second.size()){
          return true;
        }
      }
    }

    for (std::map<unsigned long, uint32_t>::const_iterator i = seenUnitStart.begin();
         i != seenUnitStart.end(); i++){
      if (pidToCodec.count(i->first) && i->second > 1){
        return true;
      }
    }

    return false;
  }

  unsigned long long decodePTS(const char *data){
    unsigned long long time;
    time = ((data[0] >> 1) & 0x07);
    time <<= 15;
    time |= ((int)data[1] << 7) | ((data[2] >> 1) & 0x7F);
    time <<= 15;
    time |= ((int)data[3] << 7) | ((data[4] >> 1) & 0x7F);
    time /= 90;
    return time;
  }

  void Stream::parsePES(unsigned long tid, bool finished){
    if (!pidToCodec.count(tid)){
      return; // skip unknown codecs
    }
    std::deque<Packet> &inStream = pesStreams[tid];
    if (inStream.size() <= 1){
      HIGH_MSG("No PES packets to parse");
      return;
    }
    // Find number of packets before unit Start
    int packNum = 1;

    std::deque<Packet>::iterator curPack = inStream.begin();

    if (seenUnitStart[tid] == 2 && inStream.begin()->getUnitStart() && inStream.rbegin()->getUnitStart()){
      packNum = inStream.size() - 1;
      curPack = inStream.end();
      curPack--;
    }else{
      curPack++;
      while (curPack != inStream.end() && !curPack->getUnitStart()){
        curPack++;
        packNum++;
      }
    }
    if (!finished && curPack == inStream.end()){
      INFO_MSG("No PES packets to parse (%lu)", seenUnitStart[tid]);
      return;
    }
    
    // We now know we're deleting 1 UnitStart, so we can pop the pesPositions and lower the seenUnitStart counter.
    --(seenUnitStart[tid]);
    std::deque<unsigned long long> &inPositions = pesPositions[tid];
    uint64_t bPos = inPositions.front();
    inPositions.pop_front();

    // Create a buffer for the current PES, and remove it from the pesStreams buffer.
    int paySize = 0;

    // Loop over the packets we need, and calculate the total payload size
    curPack = inStream.begin();
    int lastCtr = curPack->getContinuityCounter() - 1;
    for (int i = 0; i < packNum; i++){
      if (curPack->getContinuityCounter() == lastCtr){
        curPack++;
        continue;
      }
      lastCtr = curPack->getContinuityCounter();
      paySize += curPack->getPayloadLength();
      curPack++;
    }
    VERYHIGH_MSG("Parsing PES for track %lu, length %i", tid, paySize);
    // allocate a buffer, do it all again, but this time also copy the data bytes over to char*
    // payload
    char *payload = (char *)malloc(paySize);
    if(!payload){
      FAIL_MSG("cannot allocate PES packet!");
      return;
    }

    paySize = 0;
    curPack = inStream.begin();
    lastCtr = curPack->getContinuityCounter() - 1;
    for (int i = 0; i < packNum; i++){
      if (curPack->getContinuityCounter() == lastCtr){
        curPack++;
        continue;
      }
      if (curPack->getContinuityCounter() - lastCtr != 1 && curPack->getContinuityCounter()){
        INFO_MSG("Parsing PES on track %d, missed %d packets", tid,
                 curPack->getContinuityCounter() - lastCtr - 1);
      }
      lastCtr = curPack->getContinuityCounter();
      memcpy(payload + paySize, curPack->getPayload(), curPack->getPayloadLength());
      paySize += curPack->getPayloadLength();
      curPack++;
    }
    inStream.erase(inStream.begin(), curPack);
    // we now have the whole PES packet in char* payload, with a total size of paySize (including
    // headers)

    // Parse the PES header
    int offset = 0;

    while (offset < paySize){
      const char *pesHeader = payload + offset;

      // Check for large enough buffer
      if ((paySize - offset) < 9 || (paySize - offset) < 9 + pesHeader[8]){
        INFO_MSG("Not enough data on track %lu (%d / %d), discarding remainder of data", tid,
                 paySize - offset, 9 + pesHeader[8]);
        break;
      }

      // Check for valid PES lead-in
      if (pesHeader[0] != 0 || pesHeader[1] != 0x00 || pesHeader[2] != 0x01){
        INFO_MSG("Invalid PES Lead in on track %lu, discarding it", tid);
        break;
      }

      // Read the payload size.
      // Note: if the payload size is 0, then we assume the pes packet will cover the entire TS
      // Unit.
      // Note: this is technically only allowed for video pes streams.
      unsigned long long realPayloadSize = (((int)pesHeader[4] << 8) | pesHeader[5]);
      if (!realPayloadSize){
        realPayloadSize = paySize; // PES header size already included here
      }else{
        realPayloadSize += 6; // add the PES header size, always 6 bytes
      }
      // realPayloadSize is now the whole packet
      // We substract PES_header_data_length, plus the 9 bytes of mandatory header bytes
      realPayloadSize -= (9 + pesHeader[8]);


      // Read the metadata for this PES Packet
      ///\todo Determine keyframe-ness
      uint64_t timeStamp = 0;

      int64_t timeOffset = 0;
      unsigned int pesOffset = 9;       // mandatory headers
      if ((pesHeader[7] >> 6) & 0x02){// Check for PTS presence
        timeStamp = decodePTS(pesHeader + pesOffset);
        pesOffset += 5;
        if (((pesHeader[7] & 0xC0) >> 6) &
            0x01){// Check for DTS presence (yes, only if PTS present)
          timeOffset = timeStamp;
          timeStamp = decodePTS(pesHeader + pesOffset);
          pesOffset += 5;
          timeOffset -= timeStamp;
        }
      }

      timeStamp += (rolloverCount[tid] * TS_PTS_ROLLOVER);

      if ((timeStamp < lastms[tid]) && ((timeStamp % TS_PTS_ROLLOVER) < 0.1 * TS_PTS_ROLLOVER) && ((lastms[tid] % TS_PTS_ROLLOVER) > 0.9 * TS_PTS_ROLLOVER)){
        ++rolloverCount[tid];
        timeStamp += TS_PTS_ROLLOVER;
      }


      if (pesHeader[7] & 0x20){// ESCR - ignored
        pesOffset += 6;
      }
      if (pesHeader[7] & 0x10){// ESR - ignored
        pesOffset += 3;
      }
      if (pesHeader[7] & 0x08){// trick mode - ignored
        pesOffset += 1;
      }
      if (pesHeader[7] & 0x04){// additional copy - ignored
        pesOffset += 1;
      }
      if (pesHeader[7] & 0x02){// crc - ignored
        pesOffset += 2;
      }

      if (paySize - offset - pesOffset < realPayloadSize){
        WARN_MSG("Packet loss detected (%lu != %lu), glitches will occur", (uint32_t)(paySize-offset-pesOffset), (uint32_t)realPayloadSize);
        realPayloadSize = paySize - offset - pesOffset;
      }

      const char *pesPayload = pesHeader + pesOffset;
      parseBitstream(tid, pesPayload, realPayloadSize, timeStamp, timeOffset, bPos, pesHeader[6] & 0x04 );
      lastms[tid] = timeStamp;

      // Shift the offset by the payload size, the mandatory headers and the optional
      // headers/padding
      offset += realPayloadSize + (9 + pesHeader[8]);
    }
    free(payload);
  }

  void Stream::setLastms(unsigned long tid, uint64_t timestamp){
    lastms[tid] = timestamp;
    rolloverCount[tid] = timestamp / TS_PTS_ROLLOVER;
  }

  void Stream::parseBitstream(uint32_t tid, const char *pesPayload, uint32_t realPayloadSize,
                              uint64_t timeStamp, int64_t timeOffset, uint64_t bPos, bool alignment){
//INFO_MSG("timestamp: %llu offset: %lld", timeStamp, timeOffset);

    // Create a new (empty) DTSC Packet at the end of the buffer
    unsigned long thisCodec = pidToCodec[tid];
    std::deque<DTSC::Packet> & out = outPackets[tid];
    if (thisCodec == AAC){
      // Parse all the ADTS packets
      unsigned long offsetInPes = 0;
      uint64_t msRead = 0;

      if (remainders.count(tid) && remainders[tid].getLength()){
        offsetInPes =
            std::min((unsigned long)(remainders[tid].getTodo()), (unsigned long)realPayloadSize);
        remainders[tid].append(pesPayload, offsetInPes);

        if (remainders[tid].isComplete()){
          aac::adts adtsPack(remainders[tid].getData(), remainders[tid].getLength());
          if (adtsPack){
            if (!adtsInfo.count(tid) || !adtsInfo[tid].sameHeader(adtsPack)){
              MEDIUM_MSG("Setting new ADTS header: %s", adtsPack.toPrettyString().c_str());
              adtsInfo[tid] = adtsPack;
            }
            out.push_back(DTSC::Packet());
            out.back().genericFill(
                timeStamp - ((adtsPack.getSampleCount() * 1000) / adtsPack.getFrequency()),
                timeOffset, tid, adtsPack.getPayload(), adtsPack.getPayloadSize(),
                remainders[tid].getBpos(), 0);
          }
          remainders[tid].clear();
        }
      }
      while (offsetInPes < realPayloadSize){
        aac::adts adtsPack(pesPayload + offsetInPes, realPayloadSize - offsetInPes);
        if (adtsPack && adtsPack.getCompleteSize() + offsetInPes <= realPayloadSize){
          if (!adtsInfo.count(tid) || !adtsInfo[tid].sameHeader(adtsPack)){
            MEDIUM_MSG("Setting new ADTS header: %s", adtsPack.toPrettyString().c_str());
            adtsInfo[tid] = adtsPack;
          }
          out.push_back(DTSC::Packet());
          out.back().genericFill(timeStamp + msRead, timeOffset, tid,
                                             adtsPack.getPayload(), adtsPack.getPayloadSize(), bPos,
                                             0);
          msRead += (adtsPack.getSampleCount() * 1000) / adtsPack.getFrequency();
          offsetInPes += adtsPack.getCompleteSize();
        }else{
          /// \todo What about the case that we have an invalid start, going over the PES boundary?
          if (!adtsPack.hasSync()){
            offsetInPes++;
          }else{
            // remainder, keep it, use it next time
            remainders[tid].setRemainder(adtsPack, pesPayload + offsetInPes,
                                         realPayloadSize - offsetInPes, bPos);
            offsetInPes = realPayloadSize; // skip to end of PES
          }
        }
      }
    }
    if (thisCodec == ID3 || thisCodec == AC3 || thisCodec == MP2){
      out.push_back(DTSC::Packet());
      out.back().genericFill(timeStamp, timeOffset, tid, pesPayload, realPayloadSize,
                                         bPos, 0);
      if (thisCodec == MP2 && !mp2Hdr.count(tid)){
        mp2Hdr[tid] = std::string(pesPayload, realPayloadSize);
      }

    }

    if (thisCodec == H264 || thisCodec == H265){
      const char *nextPtr;
      const char *pesEnd = pesPayload+realPayloadSize;
      bool isKeyFrame = false;
      uint32_t nalSize = 0;



      nextPtr = nalu::scanAnnexB(pesPayload, realPayloadSize);
      if (!nextPtr){
        nextPtr = pesEnd;
        nalSize = realPayloadSize;
        if(!alignment && timeStamp && buildPacket.count(tid) && timeStamp != buildPacket[tid].getTime()){
          FAIL_MSG("No startcode in packet @ %llu ms, and time is not equal to %llu ms so can't merge", timeStamp, buildPacket[tid].getTime());
          return;
        }
        if (alignment){
          // If the timestamp differs from current PES timestamp, send the previous packet out and
          // fill a new one.
          if (buildPacket[tid].getTime() != timeStamp){
            // Add the finished DTSC packet to our output buffer
            out.push_back(buildPacket[tid]);

            uint32_t size;
            char * tmp ;
            buildPacket[tid].getString("data", tmp, size);

            INFO_MSG("buildpacket: size: %d, timestamp: %llu", size, buildPacket[tid].getTime())

            // Create a new empty packet with the key frame bit set to true
            buildPacket[tid].null();
            buildPacket[tid].genericFill(timeStamp, timeOffset, tid, 0, 0, bPos, true);
            buildPacket[tid].setKeyFrame(false);
          }
        
          if (!buildPacket.count(tid)){
            buildPacket[tid].genericFill(timeStamp, timeOffset, tid, 0, 0, bPos, true);
            buildPacket[tid].setKeyFrame(false);
          }

          // Check if this is a keyframe
          parseNal(tid, pesPayload, nextPtr, isKeyFrame);
          // If yes, set the keyframe flag
          if (isKeyFrame){
            buildPacket[tid].setKeyFrame(true);
          }

          // No matter what, now append the current NAL unit to the current packet
          buildPacket[tid].appendNal(pesPayload, nalSize);
        }else{
          buildPacket[tid].upgradeNal(pesPayload, nalSize);
          return;
        }
      }

      while (nextPtr < pesEnd){
        if (!nextPtr){nextPtr = pesEnd;}
        //Calculate size of NAL unit, removing null bytes from the end
        nalSize = nalu::nalEndPosition(pesPayload, nextPtr - pesPayload) - pesPayload;

        if (nalSize){
          // If we don't have a packet yet, init an empty packet with the key frame bit set to true
          if (!buildPacket.count(tid)){
            buildPacket[tid].genericFill(timeStamp, timeOffset, tid, 0, 0, bPos, true);
            buildPacket[tid].setKeyFrame(false);
          }

          // Check if this is a keyframe
          parseNal(tid, pesPayload, nextPtr, isKeyFrame);
          // If yes, set the keyframe flag
          if (isKeyFrame){
            buildPacket[tid].setKeyFrame(true);
          }

          // If the timestamp differs from current PES timestamp, send the previous packet out and
          // fill a new one.
          if (buildPacket[tid].getTime() != timeStamp){
            // Add the finished DTSC packet to our output buffer
            out.push_back(buildPacket[tid]);

            uint32_t size;
            char * tmp ;
            buildPacket[tid].getString("data", tmp, size);

        //    INFO_MSG("buildpacket: size: %d, timestamp: %llu", size, buildPacket[tid].getTime())
            // Create a new empty packet with the key frame bit set to true
            buildPacket[tid].null();
            buildPacket[tid].genericFill(timeStamp, timeOffset, tid, 0, 0, bPos, true);
            buildPacket[tid].setKeyFrame(false);
          }
          // No matter what, now append the current NAL unit to the current packet
          buildPacket[tid].appendNal(pesPayload, nalSize);
        }

        if (((nextPtr - pesPayload) + 3) >= realPayloadSize){return;}//end of the line
        
        realPayloadSize -= ((nextPtr - pesPayload) + 3); // decrease the total size
        pesPayload = nextPtr + 3;

        nextPtr = nalu::scanAnnexB(pesPayload, realPayloadSize);
      }
    }
    if (thisCodec == MPEG2){
      const char *origBegin = pesPayload;
      size_t origSize = realPayloadSize;
      const char *nextPtr;
      const char *pesEnd = pesPayload+realPayloadSize;
      uint32_t nalSize = 0;

      bool isKeyFrame = false;

      nextPtr = nalu::scanAnnexB(pesPayload, realPayloadSize);
      if (!nextPtr){
        WARN_MSG("No start code found in entire PES packet!");
        return;
      }

      while (nextPtr < pesEnd){
        if (!nextPtr){nextPtr = pesEnd;}
        //Calculate size of NAL unit, removing null bytes from the end
        nalSize = nalu::nalEndPosition(pesPayload, nextPtr - pesPayload) - pesPayload;

        // Check if this is a keyframe
        parseNal(tid, pesPayload, nextPtr, isKeyFrame);

        if (((nextPtr - pesPayload) + 3) >= realPayloadSize){break;}//end of the loop
        realPayloadSize -= ((nextPtr - pesPayload) + 3); // decrease the total size
        pesPayload = nextPtr + 3;
        nextPtr = nalu::scanAnnexB(pesPayload, realPayloadSize);
      }
      out.push_back(DTSC::Packet());
      out.back().genericFill(timeStamp, timeOffset, tid, origBegin, origSize, bPos, isKeyFrame);
    }
  }

  void Stream::getPacket(unsigned long tid, DTSC::Packet &pack){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    pack.null();
    if (!hasPacket(tid)){
      ERROR_MSG("Trying to obtain a packet on track %lu, but no full packet is available", tid);
      return;
    }

    bool packetReady = outPackets.count(tid) && outPackets[tid].size();

    if (!packetReady){
      parse(tid);
      packetReady = outPackets.count(tid) && outPackets[tid].size();
    }

    if (!packetReady){
      ERROR_MSG("Track %lu: PES without valid packets?", tid);
      return;
    }

    pack = outPackets[tid].front();
    outPackets[tid].pop_front();

    if (!outPackets[tid].size()){outPackets.erase(tid);}

  }

  void Stream::parseNal(uint32_t tid, const char *pesPayload, const char *nextPtr,
                        bool &isKeyFrame){
    bool firstSlice = true;
    char typeNal;

    if (pidToCodec[tid] == MPEG2){
      typeNal = pesPayload[0];
      switch (typeNal){
        case 0xB3:
          if (!mpeg2SeqHdr.count(tid)){
            mpeg2SeqHdr[tid] = std::string(pesPayload, (nextPtr - pesPayload));
          }
          break;
        case 0xB5:
          if (!mpeg2SeqExt.count(tid)){
            mpeg2SeqExt[tid] = std::string(pesPayload, (nextPtr - pesPayload));
          }
          break;
        case 0xB8:
          isKeyFrame = true;
          break;
      }
      return;
    }

    isKeyFrame = false;
    if (pidToCodec[tid] == H264){
      typeNal = pesPayload[0] & 0x1F;
      switch (typeNal){
      case 0x01:{
        if (firstSlice){
          firstSlice = false;
          if (!isKeyFrame){
            Utils::bitstream bs;
            for (size_t i = 1; i < 10 && i < (nextPtr - pesPayload); i++){
              if (i + 2 < (nextPtr - pesPayload) &&
                  (memcmp(pesPayload + i, "\000\000\003", 3) == 0)){// Emulation prevention bytes
                bs.append(pesPayload + i, 2);
                i += 2;
              }else{
                bs.append(pesPayload + i, 1);
              }
            }
            bs.getExpGolomb(); // Discard first_mb_in_slice
            uint64_t sliceType = bs.getUExpGolomb();
            if (sliceType == 2 || sliceType == 4 || sliceType == 7 || sliceType == 9){
              isKeyFrame = true;
            }
          }
        }
        break;
      }
      case 0x05:{
        isKeyFrame = true;
        break;
      }
      case 0x07:{
        spsInfo[tid] = std::string(pesPayload, (nextPtr - pesPayload));
        break;
      }
      case 0x08:{
        ppsInfo[tid] = std::string(pesPayload, (nextPtr - pesPayload));
        break;
      }
      default: break;
      }
    }else if (pidToCodec[tid] == H265){
      typeNal = (((pesPayload[0] & 0x7E) >> 1) & 0xFF);
      switch (typeNal){
      case 2:
      case 3: // TSA Picture
      case 4:
      case 5: // STSA Picture
      case 6:
      case 7: // RADL Picture
      case 8:
      case 9: // RASL Picture
      case 16:
      case 17:
      case 18: // BLA Picture
      case 19:
      case 20:   // IDR Picture
      case 21:{// CRA Picture
        isKeyFrame = true;
        break;
      }
      case 32:
      case 33:
      case 34:{
        tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
        hevcInfo[tid].addUnit(std::string(pesPayload, nextPtr - pesPayload)); // may i convert to (char *)?
        break;
      }
      default: break;
      }
    }
  }

  void Stream::getEarliestPacket(DTSC::Packet &pack){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    pack.null();

    unsigned long packTime = 0xFFFFFFFFull;
    unsigned long packTrack = 0;

    for (std::map<unsigned long, std::deque<DTSC::Packet> >::iterator it = outPackets.begin();
         it != outPackets.end(); it++){
      if (it->second.front().getTime() < packTime){
        packTrack = it->first;
        packTime = it->second.front().getTime();
      }
    }

    if (packTrack){getPacket(packTrack, pack);}
  }

  void Stream::initializeMetadata(DTSC::Meta &meta, unsigned long tid, unsigned long mappingId){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);

    unsigned long mId = mappingId;

    for (std::map<unsigned long, unsigned long>::const_iterator it = pidToCodec.begin();
         it != pidToCodec.end(); it++){
      if (tid && it->first != tid){continue;}

      if (mId == 0){mId = it->first;}

      if (meta.tracks.count(mId) && meta.tracks[mId].codec.size()){continue;}

      switch (it->second){
      case H264:{
        if (!spsInfo.count(it->first) || !ppsInfo.count(it->first)){
          MEDIUM_MSG("Aborted meta fill for h264 track %lu: no SPS/PPS", it->first);
          continue;
        }
        meta.tracks[mId].type = "video";
        meta.tracks[mId].codec = "H264";
        meta.tracks[mId].trackID = mId;
        std::string tmpBuffer = spsInfo[it->first];
        h264::sequenceParameterSet sps(spsInfo[it->first].data(), spsInfo[it->first].size());
        h264::SPSMeta spsChar = sps.getCharacteristics();
        meta.tracks[mId].width = spsChar.width;
        meta.tracks[mId].height = spsChar.height;
        meta.tracks[mId].fpks = spsChar.fps * 1000;
        MP4::AVCC avccBox;
        avccBox.setVersion(1);
        avccBox.setProfile(spsInfo[it->first][1]);
        avccBox.setCompatibleProfiles(spsInfo[it->first][2]);
        avccBox.setLevel(spsInfo[it->first][3]);
        avccBox.setSPSNumber(1);
        avccBox.setSPS(spsInfo[it->first]);
        avccBox.setPPSNumber(1);
        avccBox.setPPS(ppsInfo[it->first]);
        meta.tracks[mId].init = std::string(avccBox.payload(), avccBox.payloadSize());
      }break;
      case H265:{
        if (!hevcInfo.count(it->first) || !hevcInfo[it->first].haveRequired()){
          MEDIUM_MSG("Aborted meta fill for hevc track %lu: no info nal unit", it->first);
          continue;
        }
        meta.tracks[mId].type = "video";
        meta.tracks[mId].codec = "HEVC";
        meta.tracks[mId].trackID = mId;
        meta.tracks[mId].init = hevcInfo[it->first].generateHVCC();
        int pmtCount = associationTable.getProgramCount();
        for (int i = 0; i < pmtCount; i++){
          int pid = associationTable.getProgramPID(i);
          ProgramMappingEntry entry = mappingTable[pid].getEntry(0);
          while (entry){
            if (entry.getElementaryPid() == tid){
              meta.tracks[mId].lang =
                  ProgramDescriptors(entry.getESInfo(), entry.getESInfoLength()).getLanguage();
            }
            entry.advance();
          }
        }
      }break;
      case MPEG2:{
        meta.tracks[mId].type = "video";
        meta.tracks[mId].codec = "MPEG2";
        meta.tracks[mId].trackID = mId;
        meta.tracks[mId].init = std::string("\000\000\001", 3) + mpeg2SeqHdr[it->first] + std::string("\000\000\001", 3) + mpeg2SeqExt[it->first];

        Mpeg::MPEG2Info info = Mpeg::parseMPEG2Header(meta.tracks[mId].init);
        meta.tracks[mId].width = info.width;
        meta.tracks[mId].height = info.height;
        meta.tracks[mId].fpks = info.fps * 1000;
      }break;
      case ID3:{
        meta.tracks[mId].type = "meta";
        meta.tracks[mId].codec = "ID3";
        meta.tracks[mId].trackID = mId;
        meta.tracks[mId].init = metaInit[it->first];
      }break;
      case AC3:{
        meta.tracks[mId].type = "audio";
        meta.tracks[mId].codec = "AC3";
        meta.tracks[mId].trackID = mId;
        meta.tracks[mId].size = 16;
        ///\todo Fix these 2 values
        meta.tracks[mId].rate = 0;
        meta.tracks[mId].channels = 0;
      }break;
      case MP2:{
        meta.tracks[mId].type = "audio";
        meta.tracks[mId].codec = "MP2";
        meta.tracks[mId].trackID = mId;

        Mpeg::MP2Info info = Mpeg::parseMP2Header(mp2Hdr[it->first]);
        meta.tracks[mId].rate = info.sampleRate;
        meta.tracks[mId].channels = info.channels;

        ///\todo Fix this value
        meta.tracks[mId].size = 0;
      }break;
      case AAC:{
        meta.tracks[mId].type = "audio";
        meta.tracks[mId].codec = "AAC";
        meta.tracks[mId].trackID = mId;
        meta.tracks[mId].size = 16;
        meta.tracks[mId].rate = adtsInfo[it->first].getFrequency();
        meta.tracks[mId].channels = adtsInfo[it->first].getChannelCount();
        char audioInit[2]; // 5 bits object type, 4 bits frequency index, 4 bits channel index
        audioInit[0] = ((adtsInfo[it->first].getAACProfile() & 0x1F) << 3) |
                       ((adtsInfo[it->first].getFrequencyIndex() & 0x0E) >> 1);
        audioInit[1] = ((adtsInfo[it->first].getFrequencyIndex() & 0x01) << 7) |
                       ((adtsInfo[it->first].getChannelConfig() & 0x0F) << 3);
        meta.tracks[mId].init = std::string(audioInit, 2);
      }break;
      }

      int pmtCount = associationTable.getProgramCount();
      for (int i = 0; i < pmtCount; i++){
        int pid = associationTable.getProgramPID(i);
        ProgramMappingEntry entry = mappingTable[pid].getEntry(0);
        while (entry){
          if (entry.getElementaryPid() == tid){
            meta.tracks[mId].lang =
                ProgramDescriptors(entry.getESInfo(), entry.getESInfoLength()).getLanguage();
          }
          entry.advance();
        }
      }
      MEDIUM_MSG("Initialized track %lu as %s %s", it->first, meta.tracks[mId].codec.c_str(),
                 meta.tracks[mId].type.c_str());
    }
  }

  std::set<unsigned long> Stream::getActiveTracks(){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    std::set<unsigned long> result;
    // Track 0 is always active
    result.insert(0);
    // IF PAT updated in the last 5 seconds, check for contents
    if (Util::bootSecs() - lastPAT < 5){
      int pmtCount = associationTable.getProgramCount();
      // For each PMT
      for (int i = 0; i < pmtCount; i++){
        int pid = associationTable.getProgramPID(i);
        // Add PMT track
        result.insert(pid);
        // IF PMT updated in last 5 seconds, check for contents
        if (Util::bootSecs() - lastPMT[pid] < 5){
          ProgramMappingEntry entry = mappingTable[pid].getEntry(0);
          // Add all tracks in PMT
          while (entry){
            switch (entry.getStreamType()){
            case H264:
            case AAC:
            case H265:
            case AC3:
            case ID3:
            case MP2:
            case MPEG2:
              result.insert(entry.getElementaryPid()); break;
            default: break;
            }
            entry.advance();
          }
        }
      }
    }
    return result;
  }

  void Stream::eraseTrack(unsigned long tid){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    pesStreams.erase(tid);
    pesPositions.erase(tid);
    outPackets.erase(tid);
  }
}

