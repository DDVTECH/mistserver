#include "analyser_mp4.h"
#include <mist/bitfields.h>
#include <mist/mp4_generic.h>
#include <mist/h264.h>

void AnalyserMP4::init(Util::Config &conf){
  Analyser::init(conf);
}

AnalyserMP4::AnalyserMP4(Util::Config &conf) : Analyser(conf){
  curPos = prePos = 0;
  moofPos = moovPos = 0;
}

bool AnalyserMP4::parsePacket(){
  prePos = curPos;
  // Read in smart bursts until we have enough data
  while (isOpen() && mp4Buffer.size() < neededBytes()){
    uint64_t needed = neededBytes();
    mp4Buffer.reserve(needed);
    for (uint64_t i = mp4Buffer.size(); i < needed; ++i){
      mp4Buffer += std::cin.get();
      ++curPos;
      if (!std::cin.good()){
        mp4Buffer.erase(mp4Buffer.size() - 1, 1);
        break;
      }
    }
  }

  if (!isOpen()){
    if (!mp4Buffer.size()){return false;}
    uint64_t actSize = mp4Buffer.size();
    uint64_t nedSize = neededBytes();
    if (actSize < nedSize){
      WARN_MSG("Read partial box; appending %" PRIu64 " zeroes to parse remainder!", nedSize - actSize);
      mp4Buffer.append(nedSize - actSize, (char)0);
    }
  }

  if (mp4Data.read(mp4Buffer)){
    INFO_MSG("Read a %" PRIu64 "b %s box at position %" PRIu64, mp4Data.boxedSize(), mp4Data.getType().c_str(), prePos);

    // If we get an mdat, analyse it if we have known tracks, otherwise store it for later
    if (mp4Data.getType() == "mdat"){
      // Remember where we saw the mdat box
      mdatPos = prePos;
      if (hdrs.size()){
        // We have tracks, analyse it directly
        analyseData(mp4Data);
      }else{
        // No tracks yet, mdat is probably before the moov, we'll store a copy for later.
        mdat.assign(mp4Data.asBox(), mp4Data.boxedSize());
      }
    }

    // moof is parsed into the tracks we already have
    if (mp4Data.getType() == "moof"){
      moofPos = prePos;
      // Indicate that we're reading the next moof box to all track headers
      for (std::map<uint64_t, MP4::TrackHeader>::iterator t = hdrs.begin(); t != hdrs.end(); ++t){
        t->second.nextMoof();
      }
      // Loop over traf boxes inside the moof box
      std::deque<MP4::TRAF> trafs = ((MP4::MOOF*)&mp4Data)->getChildren<MP4::TRAF>();
      for (std::deque<MP4::TRAF>::iterator t = trafs.begin(); t != trafs.end(); ++t){
        if (!(t->getChild<MP4::TFHD>())){
          WARN_MSG("Could not find thfd box inside traf box!");
          continue;
        }
        uint32_t trackId = t->getChild<MP4::TFHD>().getTrackID();
        if (!hdrs.count(trackId)){
          WARN_MSG("Could not find matching trak box for traf box %" PRIu32 "!", trackId);
          continue;
        }
        hdrs[trackId].read(*t);
      }
    }

    // moov contains tracks; we parse it (wiping existing tracks, if any) and if we saw an mdat earlier, now analyse it.
    if (mp4Data.getType() == "moov"){
      // Remember where we saw this box
      moovPos = prePos;
      // Wipe existing headers, we got new ones.
      hdrs.clear();
      // Loop over trak boxes inside the moov box
      std::deque<MP4::TRAK> traks = ((MP4::MOOV*)&mp4Data)->getChildren<MP4::TRAK>();
      for (std::deque<MP4::TRAK>::iterator trakIt = traks.begin(); trakIt != traks.end(); trakIt++){
        // Create a temporary header, since we don't know the trackId yet...
        MP4::TrackHeader tHdr;
        tHdr.read(*trakIt);
        if (!tHdr.compatible()){
          INFO_MSG("Unsupported: %s", tHdr.sType.c_str());
        }else{
          INFO_MSG("Detected %s", tHdr.codec.c_str());
        }
        // Regardless of support, we now put it in our track header array (after all, even unsupported tracks can be analysed!)
        hdrs[tHdr.trackId].read(*trakIt);
      }
      // If we stored an mdat earlier, we can now analyse and then wipe it
      if (mdat.size()){
        MP4::Box mdatBox(mdat, false);
        analyseData(mdatBox);
        mdat.truncate(0);
      }
    }
    // No matter what box we saw, (try to) pretty-print it
    if (detail >= 2){std::cout << mp4Data.toPrettyString(0) << std::endl;}
    return true;
  }
  FAIL_MSG("Could not read box at position %" PRIu64, prePos);
  return false;
}

/// Calculates how many bytes we need to read a whole box.
uint64_t AnalyserMP4::neededBytes(){
  if (mp4Buffer.size() < 4){return 4;}
  uint64_t size = Bit::btohl(mp4Buffer.data());
  if (size != 1){return size;}
  if (mp4Buffer.size() < 16){return 16;}
  size = Bit::btohll(mp4Buffer.data() + 8);
  return size;
}

void printByteRange(const char * ptr, size_t start, size_t end){
  size_t byteCounter = 0;
  for (uint64_t j = start; j < end; ++j){
    if (byteCounter && (byteCounter % 32) == 0){std::cout << std::endl;}
    std::cout << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)ptr[j];
    ++byteCounter;
  }
  std::cout << std::dec << std::endl;
}

h264::nalUnit * getNalUnit(const char * data, size_t pktLen){
    switch (data[0] & 0x1F){
    case 1:
    case 5:
    case 19: return new h264::codedSliceUnit(data, pktLen);
    case 6: return new h264::seiUnit(data, pktLen);
    case 7: return new h264::spsUnit(data, pktLen);
    case 8: return new h264::ppsUnit(data, pktLen);
    default: return new h264::nalUnit(data, pktLen);
    }
}

void AnalyserMP4::analyseData(MP4::Box & mdatBox){
  // Abort if we have no headers
  if (!hdrs.size()){return;}
  // Loop over known headers
  for (std::map<uint64_t, MP4::TrackHeader>::iterator t = hdrs.begin(); t != hdrs.end(); ++t){
    size_t noPkts = t->second.size();
    for (size_t i = 0; i < noPkts; ++i){
      uint64_t offset = 0, time = 0;
      int32_t timeOffset = 0;
      uint32_t size = 0;
      bool keyFrame = false;
      t->second.getPart(i, &offset, &size, &time, &timeOffset, &keyFrame, moofPos);
      // Update mediaTime with last parsed packet, if time increased
      if (time > mediaTime){mediaTime = time;}
      std::cout << "Packet " << i << " for track " << t->first << " (" << t->second.codec << ")";
      if (keyFrame){std::cout << " (KEY)";}
      std::cout << ": " << size << "b @" << offset << ", T " << time;
      if (timeOffset){
        if (timeOffset > 0){
          std::cout << "+" << timeOffset;
        }else{
          std::cout << timeOffset;
        }
      }
      std::cout << std::endl;
      if (offset < mdatPos){
        std::cout << "Data is before mdat!" << std::endl;
        continue;
      }
      if (offset - mdatPos + size > mdatBox.boxedSize()){
        std::cout << "Data is after mdat!" << std::endl;
        continue;
      }
      if (detail < 4){continue;}
      const char * ptr = mdatBox.asBox() - mdatPos + offset;
      if (t->second.codec == "H264"){
        size_t j = 0;
        while (j+4 <= size){
          uint32_t len = Bit::btohl(ptr+j);
          std::cout << len << " bytes: ";
          printByteRange(ptr, j, 4);
          if (j+4+len > size){len = size-j-4;}

          h264::nalUnit * nalu = getNalUnit(ptr+j+4, len);
          nalu->toPrettyString(std::cout);
          delete nalu;
          if (detail > 5){printByteRange(ptr, j+4, j+4+len);}
          j += 4 + len;
        }
      }else{
        if (detail > 5){printByteRange(ptr, 0, size);}
      }
    }
  }
}

