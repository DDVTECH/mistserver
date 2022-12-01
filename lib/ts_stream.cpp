#include "defines.h"
#include "h264.h"
#include "h265.h"
#include "mp4_generic.h"
#include "mpeg.h"
#include "nal.h"
#include "ts_stream.h"
#include <stdint.h>
#include <sys/stat.h>
#include "tinythread.h"
#include "opus.h"

tthread::recursive_mutex tMutex;


namespace TS{

  Assembler::Assembler(){
    isLive = false;
  }

  void Assembler::setLive(bool live){
    isLive = live;
  }

  bool Assembler::assemble(Stream & TSStrm, const char * ptr, size_t len, bool parse, uint64_t bytePos){
    bool ret = false;
    size_t offset = 0;
    size_t amount = 188-leftData.size();
    if (leftData.size()){
      if (len >= amount){
        //Attempt to re-assemble a packet from the leftovers of last time + current head
        if (len == amount || ptr[amount] == 0x47){
          VERYHIGH_MSG("Assembled scrap packet");
          //Success!
          bytePos -= leftData.size();
          leftData.append(ptr, amount);
          tsBuf.FromPointer(leftData);
          if (!ret && tsBuf.getUnitStart()){ret = true;}
          if (parse){
            TSStrm.parse(tsBuf, isLive?0:bytePos);
          }else{
            TSStrm.add(tsBuf);
            if (!TSStrm.isDataTrack(tsBuf.getPID())){TSStrm.parse(tsBuf.getPID());}
          }
          offset = amount;
          bytePos += 188;
          leftData.truncate(0);
        }
      }else{
        //No way to verify, we'll just append and hope for the best...
        leftData.append(ptr, len);
        return ret;
      }
      //On failure, hope we might live to succeed another day
    }
    // Try to read full TS Packets
    // Watch out! We push here to a global, in order for threads to be able to access it.
    size_t junk = 0;
    while (offset < len){
      if (ptr[offset] == 0x47 && (offset+188 >= len || ptr[offset+188] == 0x47)){// check for sync byte
        if (junk){
          INFO_MSG("%zu bytes of non-sync-byte data received", junk);
          junk = 0;
        }
        if (offset + 188 <= len){
          tsBuf.FromPointer(ptr + offset);
          if (!ret && tsBuf.getUnitStart()){ret = true;}
          if (parse){
            TSStrm.parse(tsBuf, isLive?0:bytePos);
          }else{
            TSStrm.add(tsBuf);
            if (!TSStrm.isDataTrack(tsBuf.getPID())){TSStrm.parse(tsBuf.getPID());}
          }
        }else{
          leftData.assign(ptr + offset, len - offset);
        }
        bytePos += 188;
        offset += 188;
      }else{
        ++junk;
        ++offset;
        ++bytePos;
      }
    }
    return ret;
  }

  void Assembler::clear(){
    leftData.truncate(0);
  }

  void ADTSRemainder::setRemainder(const aac::adts &p, const void *source, uint32_t avail, uint64_t bPos){
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

  uint64_t ADTSRemainder::getLength(){return len;}

  uint64_t ADTSRemainder::getBpos(){return bpos;}

  uint64_t ADTSRemainder::getTodo(){return len - now;}
  char *ADTSRemainder::getData(){return data;}

  Stream::Stream(){
    psCache = 0;
    psCacheTid = 0;
  }

  Stream::~Stream(){}

  void Stream::parse(char *newPack, uint64_t bytePos){
    Packet newPacket;
    newPacket.FromPointer(newPack);
    parse(newPacket, bytePos);
  }

  void Stream::partialClear(){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    pesStreams.clear();
    psCacheTid = 0;
    psCache = 0;
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

    for (std::map<size_t, std::deque<Packet> >::const_iterator i = pesStreams.begin();
         i != pesStreams.end(); i++){
      parsePES(i->first, true);
    }
  }

  void Stream::add(char *newPack, uint64_t bytePos){
    Packet newPacket;
    newPacket.FromPointer(newPack);
    add(newPacket, bytePos);
  }

  void Stream::add(Packet &newPack, uint64_t bytePos){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    uint32_t tid = newPack.getPID();
    bool unitStart = newPack.getUnitStart();
    static uint32_t wantPrev = 0;
    bool isData = pidToCodec.count(tid);
    bool wantTrack = ((wantPrev == tid) || (tid == 0 || newPack.isPMT(pmtTracks) || isData));
    if (!wantTrack){return;}
    if (psCacheTid != tid || !psCache){
      psCache = &(pesStreams[tid]);
      psCacheTid = tid;
    }
    if (unitStart || !psCache->empty()){
      wantPrev = tid;
      psCache->push_back(newPack);
      if (unitStart && isData){
        pesPositions[tid].push_back(bytePos);
        ++(seenUnitStart[tid]);
      }
    }
  }

  bool Stream::isDataTrack(size_t tid) const{
    if (tid == 0){return false;}
    {
      tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
      return pidToCodec.count(tid);
    }
  }

  void Stream::parse(size_t tid){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    if (!pesStreams.count(tid) || pesStreams[tid].size() == 0){return;}
    if (psCacheTid != tid || !psCache){
      psCache = &(pesStreams[tid]);
      psCacheTid = tid;
    }

    // Handle PAT packets
    if (tid == 0){
      ///\todo Keep track of updates in PAT instead of keeping only the last PAT as a reference
      associationTable = psCache->back();
      lastPAT = Util::bootSecs();
      associationTable.parsePIDs(pmtTracks);
      pesStreams.erase(0);
      psCacheTid = 0;
      psCache = 0;
      return;
    }

    // Ignore conditional access packets. We don't care.
    if (tid == 1){return;}

    // Handle PMT packets
    if (pmtTracks.count(tid)){
      ///\todo Keep track of updates in PMT instead of keeping only the last PMT per program as a
      /// reference
      mappingTable[tid] = psCache->back();
      lastPMT[tid] = Util::bootSecs();
      ProgramMappingEntry entry = mappingTable[tid].getEntry(0);
      while (entry){
        uint32_t pid = entry.getElementaryPid();
        uint32_t sType = entry.getStreamType();
        switch (sType){
        case H264:
        case AAC:
        case H265:
        case AC3:
        case ID3:
        case MP2:
        case MPEG2:
        case OPUS:
        case META:{
          pidToCodec[pid] = sType;
          std::string & init = metaInit[pid];
          init.assign(entry.getESInfo(), entry.getESInfoLength());
          if (sType == META){
            TS::ProgramDescriptors desc(init.data(), init.size());
            std::string reg = desc.getRegistration();
            if (reg == "Opus"){
              pidToCodec[pid] = OPUS;
            }else{
              pidToCodec.erase(pid);
            }
          }
        } break;
        default: break;
        }
        entry.advance();
      }

      pesStreams.erase(tid);
      psCacheTid = 0;
      psCache = 0;
      return;
    }

    if (!pidToCodec.count(tid)){
      pesStreams.erase(tid);
      pesPositions.erase(tid);
      seenUnitStart.erase(tid);
      psCacheTid = 0;
      psCache = 0;
      return; // skip unknown codecs
    }

    while (seenUnitStart[tid] > 1){parsePES(tid);}
  }

  void Stream::parse(Packet &newPack, uint64_t bytePos){
    add(newPack, bytePos);
    unsigned int pid = newPack.getPID();
    if (!pid || newPack.getUnitStart()){parse(pid);}
  }

  bool Stream::hasPacketOnEachTrack() const{
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    if (!pidToCodec.size()){
      // INFO_MSG("no packet on each track 1, pidtocodec.size: %d, outpacket.size: %d",
      // pidToCodec.size(), outPackets.size());
      return false;
    }
    size_t missing = 0;
    uint64_t firstTime = 0xffffffffffffffffull, lastTime = 0;
    for (std::map<size_t, uint32_t>::const_iterator it = pidToCodec.begin(); it != pidToCodec.end(); it++){
      if (!hasPacket(it->first) || !outPackets.count(it->first) || !outPackets.at(it->first).size()){
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

  bool Stream::hasPacket(size_t tid) const{
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    if (psCacheTid != tid && pesStreams.find(tid) == pesStreams.end()){return false;}
    if (outPackets.count(tid) && outPackets.at(tid).size()){return true;}
    if (pidToCodec.count(tid) && seenUnitStart.count(tid) && seenUnitStart.at(tid) > 1){
      return true;
    }
    return false;
  }

  bool Stream::hasPacket() const{
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    if (!pesStreams.size()){return false;}

    if (outPackets.size()){
      for (std::map<size_t, std::deque<DTSC::Packet> >::const_iterator i = outPackets.begin();
           i != outPackets.end(); i++){
        if (i->second.size()){return true;}
      }
    }

    for (std::map<size_t, uint32_t>::const_iterator i = seenUnitStart.begin(); i != seenUnitStart.end(); i++){
      if (pidToCodec.count(i->first) && i->second > 1){
        return true;
      }
    }

    return false;
  }

  uint64_t decodePTS(const char *data){
    uint64_t time;
    time = ((data[0] >> 1) & 0x07);
    time <<= 15;
    time |= ((uint32_t)data[1] << 7) | ((data[2] >> 1) & 0x7F);
    time <<= 15;
    time |= ((uint32_t)data[3] << 7) | ((data[4] >> 1) & 0x7F);
    time /= 90;
    return time;
  }

  void Stream::parsePES(size_t tid, bool finished){
    if (!pidToCodec.count(tid)){
      return; // skip unknown codecs
    }
    if (psCacheTid != tid || !psCache){
      psCache = &(pesStreams[tid]);
      psCacheTid = tid;
    }
    if (!psCache->size() || (!finished && psCache->size() <= 1)){
      if (!finished){FAIL_MSG("No PES packets to parse");}
      seenUnitStart[tid] = 0;
      return;
    }
    // Find number of packets before unit Start
    size_t packNum = 1;
    std::deque<Packet>::iterator curPack = psCache->begin();

    if (seenUnitStart[tid] == 2 && psCache->begin()->getUnitStart() && psCache->rbegin()->getUnitStart()){
      packNum = psCache->size() - 1;
      curPack = psCache->end();
      curPack--;
    }else{
      curPack++;
      while (curPack != psCache->end() && !curPack->getUnitStart()){
        curPack++;
        packNum++;
      }
    }
    if (!finished && curPack == psCache->end()){
      FAIL_MSG("No PES packets to parse (%" PRIu32 ")", seenUnitStart[tid]);
      seenUnitStart[tid] = 0;
      return;
    }

    // We now know we're deleting 1 UnitStart, so we can pop the pesPositions and lower the seenUnitStart counter.
    --(seenUnitStart[tid]);
    std::deque<uint64_t> &inPositions = pesPositions[tid];
    uint64_t bPos = inPositions.front();
    inPositions.pop_front();

    // Create a buffer for the current PES, and remove it from the pesStreams buffer.
    uint32_t paySize = 0;

    // Loop over the packets we need, and calculate the total payload size
    curPack = psCache->begin();
    int lastCtr = curPack->getContinuityCounter() - 1;
    for (size_t i = 0; i < packNum; i++){
      if (curPack->getContinuityCounter() == lastCtr){
        curPack++;
        continue;
      }
      lastCtr = curPack->getContinuityCounter();
      paySize += curPack->getPayloadLength();
      curPack++;
    }
    VERYHIGH_MSG("Parsing PES for track %zu, length %" PRIu32, tid, paySize);
    // allocate a buffer, do it all again, but this time also copy the data bytes over to char*
    // payload
    char *payload = (char *)malloc(paySize);
    if (!payload){
      FAIL_MSG("cannot allocate PES packet!");
      return;
    }

    paySize = 0;
    curPack = psCache->begin();
    lastCtr = curPack->getContinuityCounter() - 1;
    for (int i = 0; i < packNum; i++){
      if (curPack->getContinuityCounter() == lastCtr){
        curPack++;
        continue;
      }
      if (curPack->getContinuityCounter() - lastCtr != 1 && curPack->getContinuityCounter()){
        INFO_MSG("Parsing PES on track %zu, missed %d packets", tid,
                 curPack->getContinuityCounter() - lastCtr - 1);
      }
      lastCtr = curPack->getContinuityCounter();
      memcpy(payload + paySize, curPack->getPayload(), curPack->getPayloadLength());
      paySize += curPack->getPayloadLength();
      curPack++;
    }
    psCache->erase(psCache->begin(), curPack);
    // we now have the whole PES packet in char* payload, with a total size of paySize (including
    // headers)

    // Parse the PES header
    uint32_t offset = 0;

    while (offset < paySize){
      const char *pesHeader = payload + offset;

      // Check for large enough buffer
      if ((paySize - offset) < 9 || (paySize - offset) < 9 + pesHeader[8]){
        INFO_MSG("Not enough data (%d / %d) on track %zu (%" PRIu32 "), discarding remainder of data",
                 paySize - offset, 9 + pesHeader[8], tid, pidToCodec[tid]);
        break;
      }

      // Check for valid PES lead-in
      if (pesHeader[0] != 0 || pesHeader[1] != 0x00 || pesHeader[2] != 0x01){
        INFO_MSG("Invalid PES Lead in on track %zu, discarding it", tid);
        break;
      }

      // Read the payload size.
      // Note: if the payload size is 0, then we assume the pes packet will cover the entire TS
      // Unit.
      // Note: this is technically only allowed for video pes streams.
      uint64_t realPayloadSize = Bit::btohs(pesHeader + 4);
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
      uint64_t pesOffset = 9;           // mandatory headers
      if ((pesHeader[7] >> 6) & 0x02){// Check for PTS presence
        timeStamp = decodePTS(pesHeader + pesOffset);
        pesOffset += 5;
        if (((pesHeader[7] & 0xC0) >> 6) & 0x01){// Check for DTS presence (yes, only if PTS present)
          timeOffset = timeStamp;
          timeStamp = decodePTS(pesHeader + pesOffset);
          pesOffset += 5;
          if (timeStamp > timeOffset){
            WARN_MSG("TS packet invalid: DTS > PTS. Ignoring DTS value.");
            timeStamp = timeOffset;
          }else{
            timeOffset -= timeStamp;
          }
        }
      }

      timeStamp += (rolloverCount[tid] * TS_PTS_ROLLOVER);

      if ((timeStamp < lastms[tid]) && ((timeStamp % TS_PTS_ROLLOVER) < 0.1 * TS_PTS_ROLLOVER) &&
          ((lastms[tid] % TS_PTS_ROLLOVER) > 0.9 * TS_PTS_ROLLOVER)){
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
        WARN_MSG("Packet loss detected (%" PRIu64 " != %" PRIu64 "), throwing away data to compensate",
                 paySize - offset - pesOffset, realPayloadSize);
        realPayloadSize = paySize - offset - pesOffset;
      }else{
        const char *pesPayload = pesHeader + pesOffset;
        parseBitstream(tid, pesPayload, realPayloadSize, timeStamp, timeOffset, bPos, pesHeader[6] & 0x04);
        lastms[tid] = timeStamp;
      }

      // Shift the offset by the payload size, the mandatory headers and the optional
      // headers/padding
      offset += realPayloadSize + (9 + pesHeader[8]);
    }
    if (finished && (pidToCodec[tid] == H264 || pidToCodec[tid] == H265)){
      if (buildPacket.count(tid) && buildPacket[tid].getDataStringLen()){
        outPackets[tid].push_back(buildPacket[tid]);
        buildPacket.erase(tid);
      }
    }
    free(payload);
  }

  void Stream::setLastms(size_t tid, uint64_t timestamp){
    lastms[tid] = timestamp;
    rolloverCount[tid] = timestamp / TS_PTS_ROLLOVER;
  }

  void Stream::parseBitstream(size_t tid, const char *pesPayload, uint64_t realPayloadSize,
                              uint64_t timeStamp, int64_t timeOffset, uint64_t bPos, bool alignment){

    // Create a new (empty) DTSC Packet at the end of the buffer
    unsigned long thisCodec = pidToCodec[tid];
    std::deque<DTSC::Packet> &out = outPackets[tid];
    if (thisCodec == AAC){
      // Parse all the ADTS packets
      uint64_t offsetInPes = 0;
      uint64_t msRead = 0;

      if (remainders.count(tid) && remainders[tid].getLength()){
        offsetInPes = std::min(remainders[tid].getTodo(), realPayloadSize);
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
                timeStamp - ((adtsPack.getSampleCount() * 1000) / adtsPack.getFrequency()), timeOffset,
                tid, adtsPack.getPayload(), adtsPack.getPayloadSize(), remainders[tid].getBpos(), 0);
          }
          remainders[tid].clear();
        }
      }
      while (offsetInPes < realPayloadSize){
        aac::adts adtsPack(pesPayload + offsetInPes, realPayloadSize - offsetInPes);
        if (adtsPack && adtsPack.getCompleteSize() + offsetInPes <= realPayloadSize){
          if (!adtsInfo.count(tid) || !adtsInfo[tid].sameHeader(adtsPack)){
            DONTEVEN_MSG("Setting new ADTS header: %s", adtsPack.toPrettyString().c_str());
            adtsInfo[tid] = adtsPack;
          }
          out.push_back(DTSC::Packet());
          if (adtsPack.getPayloadSize()){
            out.back().genericFill(timeStamp + msRead, timeOffset, tid, adtsPack.getPayload(),
                                   adtsPack.getPayloadSize(), bPos, 0);
            offsetInPes += adtsPack.getCompleteSize();
            msRead += (adtsPack.getSampleCount() * 1000) / adtsPack.getFrequency();
          }else{
            offsetInPes++;
          }
        }else{
          /// \todo What about the case that we have an invalid start, going over the PES boundary?
          if (!adtsPack){
            offsetInPes++;
          }else{
            // remainder, keep it, use it next time
            remainders[tid].setRemainder(adtsPack, pesPayload + offsetInPes, realPayloadSize - offsetInPes, bPos);
            offsetInPes = realPayloadSize; // skip to end of PES
          }
        }
      }
    }
    if (thisCodec == ID3 || thisCodec == AC3 || thisCodec == MP2 || thisCodec == META){
      out.push_back(DTSC::Packet());
      out.back().genericFill(timeStamp, timeOffset, tid, pesPayload, realPayloadSize, bPos, 0);
      if (thisCodec == MP2 && !mp2Hdr.count(tid)){
        mp2Hdr[tid] = std::string(pesPayload, realPayloadSize);
      }
    }
    if (thisCodec == OPUS){
      size_t offset = 0;
      while (realPayloadSize > offset+1){
        size_t headSize = 2;
        size_t packSize = 0;
        bool control_ext = false;
        if (pesPayload[offset+1] & 0x10){headSize += 2;}//trim start
        if (pesPayload[offset+1] & 0x08){headSize += 2;}//trim end
        if (pesPayload[offset+1] & 0x04){control_ext = true;}//control extension
        while (pesPayload[offset+2] == 255){
          packSize += 255;
          ++offset;
        }
        packSize += pesPayload[offset+2];
        ++offset;
        offset += headSize;
        //Skip control extension, if present
        if (control_ext){
          offset += pesPayload[offset] + 1;
        }
        if (realPayloadSize < offset+packSize){
          WARN_MSG("Encountered invalid Opus frame (%zu > %" PRIu64 ") - discarding!", offset+packSize, realPayloadSize);
          break;
        }
        out.push_back(DTSC::Packet());
        out.back().genericFill(timeStamp, timeOffset, tid, pesPayload+offset, packSize, bPos, 0);
        timeStamp += Opus::Opus_getDuration(pesPayload+offset);
        offset += packSize;
      }
    }
    if (thisCodec == H264 || thisCodec == H265){
      const char *nextPtr;
      const char *pesEnd = pesPayload + realPayloadSize;
      bool isKeyFrame = false;
      uint32_t nalSize = 0;

      nextPtr = nalu::scanAnnexB(pesPayload, realPayloadSize);
      if (!nextPtr){
        nextPtr = pesEnd;
        nalSize = realPayloadSize;
        if (!alignment && timeStamp && buildPacket.count(tid) && timeStamp != buildPacket[tid].getTime()){
          FAIL_MSG("No startcode in packet @ %" PRIu64 " ms, and time is not equal to %" PRIu64
                   " ms so can't merge",
                   timeStamp, buildPacket[tid].getTime());
          return;
        }
        DTSC::Packet &bp = buildPacket[tid];
        if (alignment){
          // If the timestamp differs from current PES timestamp, send the previous packet out and
          // fill a new one.
          if (bp.getTime() != timeStamp){
            // Add the finished DTSC packet to our output buffer
            out.push_back(bp);

            size_t size;
            char *tmp;
            bp.getString("data", tmp, size);

            INFO_MSG("buildpacket: size: %zu, timestamp: %" PRIu64, size, bp.getTime())

            // Create a new empty packet with the key frame bit set to true
            bp.null();
            bp.genericFill(timeStamp, timeOffset, tid, 0, 0, bPos, true);
            bp.setKeyFrame(false);
          }

          // Check if this is a keyframe
          parseNal(tid, pesPayload, nextPtr, isKeyFrame);
          // If yes, set the keyframe flag
          if (isKeyFrame){bp.setKeyFrame(true);}

          // No matter what, now append the current NAL unit to the current packet
          bp.appendNal(pesPayload, nalSize);
        }else{
          bp.upgradeNal(pesPayload, nalSize);
          return;
        }
      }

      while (nextPtr < pesEnd){
        if (!nextPtr){nextPtr = pesEnd;}
        // Calculate size of NAL unit, removing null bytes from the end
        nalSize = nalu::nalEndPosition(pesPayload, nextPtr - pesPayload) - pesPayload;

        if (nalSize){
          // If we don't have a packet yet, init an empty packet with the key frame bit set to true
          if (!buildPacket.count(tid)){
            buildPacket[tid].genericFill(timeStamp, timeOffset, tid, 0, 0, bPos, true);
            buildPacket[tid].setKeyFrame(false);
          }
          DTSC::Packet &bp = buildPacket[tid];

          // Check if this is a keyframe
          parseNal(tid, pesPayload, pesPayload + nalSize, isKeyFrame);
          // If yes, set the keyframe flag
          if (isKeyFrame){bp.setKeyFrame(true);}

          // If the timestamp differs from current PES timestamp, send the previous packet out and
          // fill a new one.
          if (bp.getTime() != timeStamp){
            // Add the finished DTSC packet to our output buffer
            out.push_back(bp);
            bp.null();
            bp.genericFill(timeStamp, timeOffset, tid, 0, 0, bPos, true);
            bp.setKeyFrame(false);
          }
          // No matter what, now append the current NAL unit to the current packet
          bp.appendNal(pesPayload, nalSize);
        }

        if (((nextPtr - pesPayload) + 3) >= realPayloadSize){return;}// end of the line

        realPayloadSize -= ((nextPtr - pesPayload) + 3); // decrease the total size
        pesPayload = nextPtr + 3;

        nextPtr = nalu::scanAnnexB(pesPayload, realPayloadSize);
      }
    }
    if (thisCodec == MPEG2){
      const char *origBegin = pesPayload;
      size_t origSize = realPayloadSize;
      const char *nextPtr;
      const char *pesEnd = pesPayload + realPayloadSize;

      bool isKeyFrame = false;

      nextPtr = nalu::scanAnnexB(pesPayload, realPayloadSize);
      if (!nextPtr){
        WARN_MSG("No start code found in entire PES packet!");
        return;
      }

      uint32_t nalno = 0;
      // We only check the first 8 packets, because keys should always be near the front of a PES.
      while (nextPtr < pesEnd && nalno < 8){
        if (!nextPtr){nextPtr = pesEnd;}
        // Calculate size of NAL unit, removing null bytes from the end
        uint32_t nalSize = nalu::nalEndPosition(pesPayload, nextPtr - pesPayload) - pesPayload;

        // Check if this is a keyframe
        parseNal(tid, pesPayload, pesPayload + nalSize, isKeyFrame);
        ++nalno;

        if (((nextPtr - pesPayload) + 3) >= realPayloadSize){break;}// end of the loop
        realPayloadSize -= ((nextPtr - pesPayload) + 3);                // decrease the total size
        pesPayload = nextPtr + 3;
        nextPtr = nalu::scanAnnexB(pesPayload, realPayloadSize);
      }
      out.push_back(DTSC::Packet());
      out.back().genericFill(timeStamp, timeOffset, tid, origBegin, origSize, bPos, isKeyFrame);
    }
  }

  void Stream::getPacket(size_t tid, DTSC::Packet &pack, size_t mappedAs){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    pack.null();
    if (!hasPacket(tid)){
      ERROR_MSG("Trying to obtain a packet on track %zu, but no full packet is available", tid);
      return;
    }

    bool packetReady = outPackets.count(tid) && outPackets[tid].size();

    if (!packetReady){
      parse(tid);
      packetReady = outPackets.count(tid) && outPackets[tid].size();
    }

    if (!packetReady){
      ERROR_MSG("Track %zu: PES without valid packets?", tid);
      return;
    }

    pack = DTSC::Packet(outPackets[tid].front(), mappedAs);
    outPackets[tid].pop_front();

    if (!outPackets[tid].size()){outPackets.erase(tid);}
  }

  void Stream::parseNal(size_t tid, const char *pesPayload, const char *nextPtr, bool &isKeyFrame){
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
      case 0xB8: isKeyFrame = true; break;
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
              if (i + 2 < (nextPtr - pesPayload) && (memcmp(pesPayload + i, "\000\000\003", 3) == 0)){// Emulation prevention bytes
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
      typeNal = (pesPayload[0] & 0x7E) >> 1;
      switch (typeNal){
      case 2:
      case 3: // TSA Picture
      case 4:
      case 5: // STSA Picture
      case 6:
      case 7: // RADL Picture
      case 8:
      case 9:{// RASL Picture
        isKeyFrame = false;
        break;
      }
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

  uint32_t Stream::getEarliestPID(){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);

    uint64_t packTime = 0xFFFFFFFFull;
    uint32_t packTrack = 0;

    for (std::map<size_t, std::deque<DTSC::Packet> >::iterator it = outPackets.begin();
         it != outPackets.end(); it++){
      if (it->second.front().getTime() < packTime){
        packTrack = it->first;
        packTime = it->second.front().getTime();
      }
    }

    return packTrack;
  }

  void Stream::getEarliestPacket(DTSC::Packet &pack){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    pack.null();

    uint64_t packTime = 0xFFFFFFFFull;
    uint64_t packTrack = 0;

    for (std::map<size_t, std::deque<DTSC::Packet> >::iterator it = outPackets.begin();
         it != outPackets.end(); it++){
      if (it->second.size() && it->second.front().getTime() < packTime){
        packTrack = it->first;
        packTime = it->second.front().getTime();
      }
    }

    if (packTrack){
      getPacket(packTrack, pack);
      return;
    }

    //Nothing yet...? Let's see if we can parse something.
    for (std::map<size_t, uint32_t>::const_iterator i = seenUnitStart.begin(); i != seenUnitStart.end(); i++){
      if (pidToCodec.count(i->first) && i->second > 1){
        parse(i->first);
        if (hasPacket(i->first)){
          getPacket(packTrack, pack);
          return;
        }
      }
    }
  }

  void Stream::initializeMetadata(DTSC::Meta &meta, size_t tid, size_t mappingId){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);

    for (std::map<size_t, uint32_t>::const_iterator it = pidToCodec.begin(); it != pidToCodec.end(); it++){
      if (tid != INVALID_TRACK_ID && it->first != tid){continue;}

      size_t mId = (mappingId == INVALID_TRACK_ID ? it->first : mappingId);

      size_t idx = meta.trackIDToIndex(mId, getpid());
      if (idx != INVALID_TRACK_ID && meta.getCodec(idx).size()){continue;}

      // We now know we have to add a new track, OR the current track still needs it metadata set
      bool addNewTrack = false;
      std::string type, codec, init;
      uint64_t width = 0, height = 0, fpks = 0, size = 0, rate = 0, channels = 0;

      switch (it->second){
      case H264:{
        if (!spsInfo.count(it->first) || !ppsInfo.count(it->first)){
          MEDIUM_MSG("Aborted meta fill for h264 track %zu: no SPS/PPS", it->first);
          continue;
        }
        // First generate needed data
        std::string tmpBuffer = spsInfo[it->first];
        h264::sequenceParameterSet sps(tmpBuffer.data(), tmpBuffer.size());
        h264::SPSMeta spsChar = sps.getCharacteristics();

        MP4::AVCC avccBox;
        avccBox.setVersion(1);
        avccBox.setProfile(spsInfo[it->first][1]);
        avccBox.setCompatibleProfiles(spsInfo[it->first][2]);
        avccBox.setLevel(spsInfo[it->first][3]);
        avccBox.setSPSCount(1);
        avccBox.setSPS(spsInfo[it->first]);
        avccBox.setPPSCount(1);
        avccBox.setPPS(ppsInfo[it->first]);

        // Then set all data for track
        addNewTrack = true;
        type = "video";
        codec = "H264";
        width = spsChar.width;
        height = spsChar.height;
        fpks = spsChar.fps * 1000;
        init.assign(avccBox.payload(), avccBox.payloadSize());
      }break;
      case H265:{
        if (!hevcInfo.count(it->first) || !hevcInfo[it->first].haveRequired()){
          MEDIUM_MSG("Aborted meta fill for hevc track %zu: no info nal unit", it->first);
          continue;
        }
        addNewTrack = true;
        type = "video";
        codec = "HEVC";
        init = hevcInfo[it->first].generateHVCC();
        h265::metaInfo metaInfo = hevcInfo[it->first].getMeta();
        width = metaInfo.width;
        height = metaInfo.height;
        fpks = metaInfo.fps * 1000;
      }break;
      case MPEG2:{
        addNewTrack = true;
        type = "video";
        codec = "MPEG2";
        init = std::string("\000\000\001", 3) + mpeg2SeqHdr[it->first] +
               std::string("\000\000\001", 3) + mpeg2SeqExt[it->first];
        Mpeg::MPEG2Info info = Mpeg::parseMPEG2Header(init);
        width = info.width;
        height = info.height;
        fpks = info.fps * 1000;
      }break;
      case ID3:{
        addNewTrack = true;
        type = "meta";
        codec = "ID3";
        init = metaInit[it->first];
      }break;
      case META:{
        addNewTrack = true;
        type = "meta";
        codec = "RAW";
        init = metaInit[it->first];
      }break;
      case AC3:{
        addNewTrack = true;
        type = "audio";
        codec = "AC3";
        size = 16;
      }break;
      case OPUS:{
        addNewTrack = true;
        type = "audio";
        codec = "opus";
        size = 16;
        init = std::string("OpusHead\001\002\170\000\200\273\000\000\000\000\001", 19);
        channels = 2;
        std::string extData = TS::ProgramDescriptors(metaInit[it->first].data(), metaInit[it->first].size()).getExtension();
        if (extData.size() > 1){
          channels = extData[1];
          uint8_t channel_map = extData[2];
          if (channels > 8){
            FAIL_MSG("Channel count %u not implemented", (int)channels);
            if (channel_map == 1){channel_map = 255;}
          }
          if (channel_map > 1){
            FAIL_MSG("Channel mapping table %u not implemented", (int)init[18]);
            channel_map = 255;
          }
          if (channels > 2 && channels <= 8 && channel_map == 0){
            WARN_MSG("Overriding channel mapping table from 0 to 1");
            channel_map = 1;
          }
          init[9] = channels;
          init[18] = channel_map;
          if (channel_map == 1){
            static const uint8_t opus_coupled_stream_cnt[9] = {1,0,1,1,2,2,2,3,3};
            static const uint8_t opus_stream_cnt[9] = {1,1,1,2,2,3,4,4,5};
            static const uint8_t opus_channel_map[8][8] = {
                {0},
                {0,1},
                {0,2,1},
                {0,1,2,3},
                {0,4,1,2,3},
                {0,4,1,2,3,5},
                {0,4,1,2,3,5,6},
                {0,6,1,2,3,4,5,7},
            };
            init += (char)opus_stream_cnt[channels];
            init += (char)opus_coupled_stream_cnt[channels];
            init += std::string("\000\000\000\000\000\000\000\000", channels);
            memcpy((char*)init.data()+21, opus_channel_map[channels - 1], channels);
          }
        }
        rate = 48000;
      }break;
      case MP2:{
        addNewTrack = true;
        Mpeg::MP2Info info = Mpeg::parseMP2Header(mp2Hdr[it->first]);
        type = "audio";
        codec = (info.layer == 3 ? "MP3" : "MP2");
        rate = info.sampleRate;
        channels = info.channels;
      }break;
      case AAC:{
        addNewTrack = true;
        init.resize(2);
        init[0] = ((adtsInfo[it->first].getAACProfile() & 0x1F) << 3) |
                  ((adtsInfo[it->first].getFrequencyIndex() & 0x0E) >> 1);
        init[1] = ((adtsInfo[it->first].getFrequencyIndex() & 0x01) << 7) |
                  ((adtsInfo[it->first].getChannelConfig() & 0x0F) << 3);
        // Wait with adding the track until we have init data
        if (init[0] == 0 && init[1] == 0){addNewTrack = false;}
        type = "audio";
        codec = "AAC";
        size = 16;
        rate = adtsInfo[it->first].getFrequency();
        channels = adtsInfo[it->first].getChannelCount();
      }break;
      }

      // Add track to meta here, if newTrack is set. Otherwise only re-initialize values
      if (idx == INVALID_TRACK_ID){
        if (!addNewTrack){return;}
        idx = meta.addTrack();
      }
      meta.setType(idx, type);
      meta.setCodec(idx, codec);
      meta.setID(idx, mId);
      if (init.size()){meta.setInit(idx, init);}
      meta.setWidth(idx, width);
      meta.setHeight(idx, height);
      meta.setFpks(idx, fpks);
      meta.setSize(idx, size);
      meta.setRate(idx, rate);
      meta.setChannels(idx, channels);

      size_t pmtCount = associationTable.getProgramCount();
      for (size_t i = 0; i < pmtCount; i++){
        uint32_t pid = associationTable.getProgramPID(i);
        ProgramMappingEntry entry = mappingTable[pid].getEntry(0);
        while (entry){
          if (entry.getElementaryPid() == tid){
            meta.setLang(idx, ProgramDescriptors(entry.getESInfo(), entry.getESInfoLength()).getLanguage());
          }
          entry.advance();
        }
      }
      MEDIUM_MSG("Initialized track %zu as %s %s", idx, codec.c_str(), type.c_str());
      if (tid != INVALID_TRACK_ID){return;}
    }
    if (tid != INVALID_TRACK_ID){
      WARN_MSG("Could not init track %zu!", tid);
      for (std::map<size_t, uint32_t>::const_iterator it = pidToCodec.begin(); it != pidToCodec.end(); it++){
        INFO_MSG("Track %zu (%" PRIu32 ") no match", it->first, it->second);
      }
    }
  }

  std::set<size_t> Stream::getActiveTracks(){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    std::set<size_t> result;
    // Track 0 is always active
    result.insert(0);
    // IF PAT updated in the last 5 seconds, check for contents
    if (Util::bootSecs() - lastPAT < 5){
      size_t pmtCount = associationTable.getProgramCount();
      // For each PMT
      for (size_t i = 0; i < pmtCount; i++){
        size_t pid = associationTable.getProgramPID(i);
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
            case OPUS:
            case META: result.insert(entry.getElementaryPid()); break;
            default: break;
            }
            entry.advance();
          }
        }
      }
    }
    return result;
  }

  void Stream::eraseTrack(size_t tid){
    tthread::lock_guard<tthread::recursive_mutex> guard(tMutex);
    pesStreams.erase(tid);
    psCacheTid = 0;
    psCache = 0;
    pesPositions.erase(tid);
    outPackets.erase(tid);
  }
}// namespace TS
