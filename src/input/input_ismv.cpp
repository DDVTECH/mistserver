#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mist/defines.h>
#include <mist/stream.h>
#include <string>

#include "input_ismv.h"

namespace Mist{
  inputISMV::inputISMV(Util::Config *cfg) : Input(cfg){
    capa["name"] = "ISMV";
    capa["desc"] = "This input allows you to stream ISMV Video on Demand files.";
    capa["source_match"] = "/*.ismv";
    capa["priority"] = 9;
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["audio"].append("AAC");

    inFile = 0;
  }

  bool inputISMV::checkArguments(){
    if (config->getString("input") == "-"){
      std::cerr << "Input from stdin not yet supported" << std::endl;
      return false;
    }
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

  bool inputISMV::preRun(){
    inFile = fopen(config->getString("input").c_str(), "r");
    return inFile; // True if not null
  }

  bool inputISMV::readHeader(){
    if (!inFile){return false;}
    meta.reInit(streamName);
    // parse ismv header
    fseek(inFile, 0, SEEK_SET);
    // Skip mandatory ftyp box
    MP4::skipBox(inFile);

    MP4::MOOV moovBox;
    moovBox.read(inFile);
    parseMoov(moovBox);

    std::map<size_t, uint64_t> duration;

    uint64_t lastBytePos = 0;
    uint64_t curBytePos = ftell(inFile);
    // parse fragments form here

    size_t tId;
    std::vector<MP4::trunSampleInformation> trunSamples;

    while (readMoofSkipMdat(tId, trunSamples) && !feof(inFile)){
      if (!duration.count(tId)){duration[tId] = 0;}
      for (std::vector<MP4::trunSampleInformation>::iterator it = trunSamples.begin();
           it != trunSamples.end(); it++){
        bool first = (it == trunSamples.begin());

        int64_t offsetConv = 0;
        if (M.getType(tId) == "video"){offsetConv = it->sampleOffset / 10000;}

        if (first){
          lastBytePos = curBytePos;
        }else{
          ++lastBytePos;
        }

        meta.update(duration[tId] / 10000, offsetConv, tId, it->sampleSize, lastBytePos, first);
        duration[tId] += it->sampleDuration;
      }
      curBytePos = ftell(inFile);
    }
    // Export DTSH to file
    Socket::Connection outFile;
    int tmpFd = open("/dev/null", O_RDWR);
    outFile.open(tmpFd);
    Util::Procs::socketList.insert(tmpFd);
    genericWriter(config->getString("input") + ".dtsh", &outFile, false);
    if (outFile){M.send(outFile, false, M.getValidTracks(), false);}
    return true;
  }

  void inputISMV::getNext(size_t idx){
    thisPacket.null();

    if (!buffered.size()){return;}

    seekPos thisPos = *buffered.begin();
    buffered.erase(buffered.begin());

    fseek(inFile, thisPos.position, SEEK_SET);
    dataPointer.allocate(thisPos.size);
    fread(dataPointer, thisPos.size, 1, inFile);

    thisPacket.genericFill(thisPos.time / 10000, thisPos.offset / 10000, thisPos.trackId,
                           dataPointer, thisPos.size, 0, thisPos.isKeyFrame);
    thisTime = thisPos.time/1000;
    thisIdx = thisPos.trackId;

    if (buffered.size() < 2 * (idx == INVALID_TRACK_ID ? M.getValidTracks().size() : 1)){
      std::set<size_t> validTracks = M.getValidTracks();
      if (idx != INVALID_TRACK_ID){
        validTracks.clear();
        validTracks.insert(idx);
      }

      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
        bufferFragmentData(*it, ++lastKeyNum[*it]);
      }
    }
    if (idx != INVALID_TRACK_ID && thisPacket.getTrackId() != M.getID(idx)){getNext(idx);}
  }

  void inputISMV::seek(uint64_t seekTime, size_t idx){
    buffered.clear();
    lastKeyNum.clear();

    // Select tracks
    std::set<size_t> validTracks = M.getValidTracks();
    if (idx != INVALID_TRACK_ID){
      validTracks.clear();
      validTracks.insert(idx);
    }

    // For each selected track
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
      DTSC::Keys keys(M.keys(*it));
      uint32_t i;
      for (i = keys.getFirstValid(); i < keys.getEndValid(); i++){
        if (keys.getTime(i) >= seekTime){break;}
      }
      INFO_MSG("ISMV seek frag %zu:%" PRIu32, *it, i);
      bufferFragmentData(*it, i);
      lastKeyNum[*it] = i;
    }
  }

  void inputISMV::parseMoov(MP4::MOOV &moovBox){
    std::deque<MP4::TRAK> trak = moovBox.getChildren<MP4::TRAK>();
    for (std::deque<MP4::TRAK>::iterator it = trak.begin(); it != trak.end(); it++){
      size_t tNumber = meta.addTrack();

      meta.setID(tNumber, it->getChild<MP4::TKHD>().getTrackID());

      MP4::MDIA mdia = it->getChild<MP4::MDIA>();

      MP4::HDLR hdlr = mdia.getChild<MP4::HDLR>();
      if (hdlr.getHandlerType() == "soun"){meta.setType(tNumber, "audio");}
      if (hdlr.getHandlerType() == "vide"){meta.setType(tNumber, "video");}

      MP4::STSD stsd = mdia.getChild<MP4::MINF>().getChild<MP4::STBL>().getChild<MP4::STSD>();
      for (size_t i = 0; i < stsd.getEntryCount(); ++i){
        if (stsd.getEntry(i).isType("mp4a") || stsd.getEntry(i).isType("enca")){
          MP4::MP4A mp4aBox = (MP4::MP4A &)stsd.getEntry(i);
          std::string tmpStr;
          tmpStr += (char)((mp4aBox.toAACInit() & 0xFF00) >> 8);
          tmpStr += (char)(mp4aBox.toAACInit() & 0x00FF);
          meta.setCodec(tNumber, "AAC");
          meta.setInit(tNumber, tmpStr);
          meta.setChannels(tNumber, mp4aBox.getChannelCount());
          meta.setSize(tNumber, mp4aBox.getSampleSize());
          meta.setRate(tNumber, mp4aBox.getSampleRate());
        }
        if (stsd.getEntry(i).isType("avc1") || stsd.getEntry(i).isType("encv")){
          MP4::AVC1 avc1Box = (MP4::AVC1 &)stsd.getEntry(i);
          meta.setCodec(tNumber, "H264");
          meta.setInit(tNumber, avc1Box.getCLAP().payload(), avc1Box.getCLAP().payloadSize());
          meta.setHeight(tNumber, avc1Box.getHeight());
          meta.setWidth(tNumber, avc1Box.getWidth());
        }
      }
    }
  }

  bool inputISMV::readMoofSkipMdat(size_t &tId, std::vector<MP4::trunSampleInformation> &trunSamples){
    tId = INVALID_TRACK_ID;
    trunSamples.clear();

    MP4::MOOF moof;
    moof.read(inFile);

    if (feof(inFile)){return false;}

    MP4::TRAF trafBox = moof.getChild<MP4::TRAF>();
    for (size_t j = 0; j < trafBox.getContentCount(); j++){
      if (trafBox.getContent(j).isType("trun")){
        MP4::TRUN trunBox = (MP4::TRUN &)trafBox.getContent(j);
        for (size_t i = 0; i < trunBox.getSampleInformationCount(); i++){
          trunSamples.push_back(trunBox.getSampleInformation(i));
        }
      }
      if (trafBox.getContent(j).isType("tfhd")){
        tId = M.trackIDToIndex(((MP4::TFHD &)trafBox.getContent(j)).getTrackID(), getpid());
      }
    }

    MP4::skipBox(inFile);
    return !feof(inFile);
  }

  void inputISMV::bufferFragmentData(size_t trackId, uint32_t keyNum){
    INFO_MSG("Bpos seek for %zu/%" PRIu32, trackId, keyNum);
    if (trackId == INVALID_TRACK_ID){return;}
    DTSC::Keys keys(M.keys(trackId));
    INFO_MSG("Key %" PRIu32 " / %zu", keyNum, keys.getEndValid());
    if (keyNum >= keys.getEndValid()){return;}
    uint64_t currentPosition = keys.getBpos(keyNum);
    uint64_t currentTime = keys.getTime(keyNum) * 10000;
    INFO_MSG("Bpos seek to %" PRIu64, currentPosition);
    fseek(inFile, currentPosition, SEEK_SET);

    MP4::MOOF moofBox;
    moofBox.read(inFile);

    MP4::TRAF trafBox = moofBox.getChild<MP4::TRAF>();

    MP4::TRUN trunBox;
    MP4::UUID_SampleEncryption uuidBox;
    for (unsigned int j = 0; j < trafBox.getContentCount(); j++){
      if (trafBox.getContent(j).isType("trun")){trunBox = (MP4::TRUN &)trafBox.getContent(j);}
      if (trafBox.getContent(j).isType("tfhd")){
        if (M.getID(trackId) != ((MP4::TFHD &)trafBox.getContent(j)).getTrackID()){
          FAIL_MSG("Trackids do not match");
          return;
        }
      }
    }

    currentPosition = ftell(inFile) + 8;
    for (unsigned int i = 0; i < trunBox.getSampleInformationCount(); i++){
      seekPos myPos;
      myPos.position = currentPosition;
      myPos.trackId = trackId;
      myPos.time = currentTime;
      myPos.duration = trunBox.getSampleInformation(i).sampleDuration;
      myPos.size = trunBox.getSampleInformation(i).sampleSize;
      if (trunBox.getFlags() & MP4::trunsampleOffsets){
        unsigned int offsetConv = trunBox.getSampleInformation(i).sampleOffset;
        myPos.offset = *(int *)&offsetConv;
      }else{
        myPos.offset = 0;
      }
      myPos.isKeyFrame = (i == 0);
      currentTime += trunBox.getSampleInformation(i).sampleDuration;
      currentPosition += trunBox.getSampleInformation(i).sampleSize;
      buffered.insert(myPos);
    }
  }
}// namespace Mist
