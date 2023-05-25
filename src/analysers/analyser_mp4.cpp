#include "analyser_mp4.h"
#include <mist/bitfields.h>
#include <mist/mp4_generic.h>
#include <mist/h264.h>


class mp4TrackHeader{
public:
  mp4TrackHeader(){
    initialised = false;
    stscStart = 0;
    sampleIndex = 0;
    deltaIndex = 0;
    deltaPos = 0;
    deltaTotal = 0;
    offsetIndex = 0;
    offsetPos = 0;
    sttsBox.clear();
    hasCTTS = false;
    cttsBox.clear();
    stszBox.clear();
    stcoBox.clear();
    co64Box.clear();
    stco64 = false;
    trackId = 0;
  }
  void read(MP4::TRAK &trakBox){
    initialised = false;
    std::string tmp; // temporary string for copying box data
    MP4::Box trakLoopPeek;
    timeScale = 1;

    MP4::MDIA mdiaBox = trakBox.getChild<MP4::MDIA>();

    timeScale = mdiaBox.getChild<MP4::MDHD>().getTimeScale();
    trackId = trakBox.getChild<MP4::TKHD>().getTrackID();

    MP4::STBL stblBox = mdiaBox.getChild<MP4::MINF>().getChild<MP4::STBL>();

    sttsBox.copyFrom(stblBox.getChild<MP4::STTS>());
    cttsBox.copyFrom(stblBox.getChild<MP4::CTTS>());
    stszBox.copyFrom(stblBox.getChild<MP4::STSZ>());
    stcoBox.copyFrom(stblBox.getChild<MP4::STCO>());
    co64Box.copyFrom(stblBox.getChild<MP4::CO64>());
    stscBox.copyFrom(stblBox.getChild<MP4::STSC>());
    stco64 = co64Box.isType("co64");
    hasCTTS = cttsBox.isType("ctts");
  }
  size_t trackId;
  MP4::STCO stcoBox;
  MP4::CO64 co64Box;
  MP4::STSZ stszBox;
  MP4::STTS sttsBox;
  bool hasCTTS;
  MP4::CTTS cttsBox;
  MP4::STSC stscBox;
  uint64_t timeScale;
  void getPart(uint64_t index, uint64_t &offset, uint64_t &size){
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
    size = stszBox.getEntrySize(index);

    initialised = true;
  }
  uint64_t size(){return (stszBox.asBox() ? stszBox.getSampleCount() : 0);}

private:
  bool initialised;
  // next variables are needed for the stsc/stco loop
  uint64_t stscStart;
  uint64_t sampleIndex;
  // next variables are needed for the stts loop
  uint64_t deltaIndex; ///< Index in STTS box
  uint64_t deltaPos;   ///< Sample counter for STTS box
  uint64_t deltaTotal; ///< Total timestamp for STTS box
  // for CTTS box loop
  uint64_t offsetIndex; ///< Index in CTTS box
  uint64_t offsetPos;   ///< Sample counter for CTTS box

  bool stco64;
};


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
    if (mp4Data.getType() == "mdat"){
      mdatPos = prePos;
      analyseData(mp4Data);
      return true;
    }
    if (mp4Data.getType() == "moof"){
      moof.assign(mp4Data.asBox(), mp4Data.boxedSize());
      moofPos = prePos;
    }
    if (mp4Data.getType() == "moov"){
      moov.assign(mp4Data.asBox(), mp4Data.boxedSize());
      moovPos = prePos;
    }
    if (detail >= 2){
      std::cout << mp4Data.toPrettyString(0) << std::endl;
    }
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
  if (moov.size()){
    MP4::Box globHdr(moov, false);
    std::deque<MP4::TRAK> traks = ((MP4::MOOV*)&globHdr)->getChildren<MP4::TRAK>();

    size_t trkCounter = 0;
    for (std::deque<MP4::TRAK>::iterator trakIt = traks.begin(); trakIt != traks.end(); trakIt++){
      trkCounter++;
      MP4::MDIA mdiaBox = trakIt->getChild<MP4::MDIA>();

      std::string hdlrType = mdiaBox.getChild<MP4::HDLR>().getHandlerType();
      if (hdlrType != "vide" && hdlrType != "soun" && hdlrType != "sbtl"){
        INFO_MSG("Unsupported handler: %s", hdlrType.c_str());
        continue;
      }

      std::string sType = mdiaBox.getChild<MP4::MINF>().getChild<MP4::STBL>().getChild<MP4::STSD>().getEntry(0).getType();
      if (sType == "avc1" || sType == "h264" || sType == "mp4v"){sType = "H264";}
      if (sType == "hev1" || sType == "hvc1"){sType = "HEVC";}
      if (sType == "ac-3"){sType = "AC3";}
      if (sType == "tx3g"){sType = "subtitle";}
      INFO_MSG("Detected %s", sType.c_str());
      mp4TrackHeader tHdr;
      tHdr.read(*trakIt);
      size_t noPkts = tHdr.size();
      for (size_t i = 0; i < noPkts; ++i){
        uint64_t offset = 0, size = 0;
        tHdr.getPart(i, offset, size);
        std::cout << "Packet " << i << " for track " << trkCounter << " (" << sType << "): " << size << " bytes" << std::endl;
        if (offset < mdatPos){
          std::cout << "Data is before mdat!" << std::endl;
          continue;
        }
        if (offset - mdatPos + size > mdatBox.boxedSize()){
          std::cout << "Data is after mdat!" << std::endl;
          continue;
        }
        const char * ptr = mdatBox.asBox() - mdatPos + offset;
        if (sType == "H264"){
          size_t j = 0;
          while (j+4 <= size){
            uint32_t len = Bit::btohl(ptr+j);
            std::cout << len << " bytes: ";
            printByteRange(ptr, j, 4);
            if (j+4+len > size){len = size-j-4;}

            h264::nalUnit * nalu = getNalUnit(ptr+j+4, len);
            nalu->toPrettyString(std::cout);
            delete nalu;
            printByteRange(ptr, j+4, j+4+len);
            j += 4 + len;
          }
        }else{
          printByteRange(ptr, 0, size);
        }

      }
    }
  }
  if (moof.size()){
  }
  ///\TODO update mediaTime with the current timestamp
}

