#include "analyser_mp4.h"
#include <mist/bitfields.h>
#include <mist/mp4_generic.h>

void AnalyserMP4::init(Util::Config &conf){
  Analyser::init(conf);
}

AnalyserMP4::AnalyserMP4(Util::Config &conf) : Analyser(conf){
  curPos = prePos = 0;
}

bool AnalyserMP4::parsePacket(){
  prePos = curPos;

  //Attempt to read a whole box
  uint64_t bytesNeeded = neededBytes();
  while (mp4Buffer.size() < bytesNeeded){
    mp4Buffer.allocate(bytesNeeded);
    uri.readSome(bytesNeeded - mp4Buffer.size(), *this);
    bytesNeeded = neededBytes();
    if (mp4Buffer.size() < bytesNeeded){
      if (uri.isEOF()){break;}
      Util::sleep(50);
    }
  }

  curPos += bytesNeeded;
  mp4Data = MP4::Box(mp4Buffer, false);
  if (!mp4Data){
    FAIL_MSG("Could not read box at position %" PRIu64, prePos);
    return false;
  }

  VERYHIGH_MSG("Read a %s box at position %" PRIu64, mp4Data.getType().c_str(), prePos);
  if (mp4Data.getType() == "moov"){
    MP4::MOOV * moov = ((MP4::MOOV*)&mp4Data);
    MP4::Box mvhdBox = moov->getChild("mvhd");
    moov->getContentCount();
    timescaleGlobal = ((MP4::MVHD*)&mvhdBox)->getTimeScale();
    MEDIUM_MSG("Global timescale: %" PRIu32, timescaleGlobal);
    std::deque<MP4::Box> traks = moov->getChildren("trak");
    while (traks.size()){
      MP4::TRAK * trak = (MP4::TRAK*)&(traks.front());
      MP4::Box tkhdBox = trak->getChild("tkhd");
      uint32_t trackId = ((MP4::TKHD*)&tkhdBox)->getTrackID();
      MP4::Box mdiaBox = trak->getChild("mdia");
      MP4::Box mdhdBox = ((MP4::MDIA*)&mdiaBox)->getChild("mdhd");
      timescaleTrack[trackId] = ((MP4::MDHD*)&mdhdBox)->getTimeScale();
      MEDIUM_MSG("Track %" PRIu32 " timescale: %" PRIu32, trackId, timescaleTrack[trackId]);
      traks.pop_front();
    }
  }
  if (mp4Data.getType() == "moof"){
    MP4::MOOF * moof = ((MP4::MOOF*)&mp4Data);
    std::deque<MP4::Box> trafs = moof->getChildren("traf");
    while (trafs.size()){
      MP4::TRAF * traf = (MP4::TRAF*)&(trafs.front());
      MP4::Box tfhdBox = traf->getChild("tfhd");
      uint32_t trackId = ((MP4::TFHD*)&tfhdBox)->getTrackID();
      std::deque<MP4::Box> truns = traf->getChildren("trun");
      while (truns.size()){
        MP4::TRUN * trun = (MP4::TRUN*)&(truns.front());
        for (uint32_t i = 0; i < trun->getSampleInformationCount(); ++i){
          MP4::trunSampleInformation samples = trun->getSampleInformation(i);
          trackTime[trackId] += samples.sampleDuration;
        }
        truns.pop_front();
      }
      uint64_t currTime = trackTime[trackId] * 1000 / timescaleTrack[trackId];
      MEDIUM_MSG("Track %" PRIu32 " is now at time %" PRIu64, trackId, currTime);
      if (currTime > mediaTime){mediaTime = currTime;}
      trafs.pop_front();
    }
  }
  //Pretty-print box if needed
  if (!validate && detail >= 2){std::cout << mp4Data.toPrettyString(0) << std::endl;}
  mp4Buffer.pop(bytesNeeded);
  return true;
}


/// Calculates how many bytes we need to read a whole box.
uint64_t AnalyserMP4::neededBytes(){
  //Ensure we can read the box size
  if (mp4Buffer.size() < 4){return 4;}
  uint64_t size = Bit::btohl(mp4Buffer);
  if (size != 1){return size;}
  //A size of 1 means this is a longer-sized box, and we should read a 64-bit size after the box type
  if (mp4Buffer.size() < 16){return 16;}
  size = Bit::btohll(mp4Buffer + 8);
  return size;
}

void AnalyserMP4::dataCallback(const char *ptr, size_t size) {
  mp4Buffer.append(ptr, size);
}

