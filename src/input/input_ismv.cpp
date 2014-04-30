#include <iostream>
#include <fstream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mist/stream.h>
#include <mist/defines.h>

#include "input_ismv.h"

namespace Mist {
  inputISMV::inputISMV(Util::Config * cfg) : Input(cfg) {
    capa["decs"] = "Enables ISMV Input";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");

    inFile = 0;
  }

  bool inputISMV::setup() {
    if (config->getString("input") == "-") {
      std::cerr << "Input from stdin not yet supported" << std::endl;
      return false;
    }
    if (!config->getBool("player")){
      if (config->getString("output") == "-") {
        std::cerr << "Output to stdout not yet supported" << std::endl;
        return false;
      }
    }else{
      if (config->getString("output") != "-") {
        std::cerr << "File output in player mode not supported" << std::endl;
        return false;
      }
    }
    
    //open File
    inFile = fopen(config->getString("input").c_str(), "r");
    if (!inFile) {
      return false;
    }
    return true;
  }

  bool inputISMV::readHeader() {
    if (!inFile) {
      return false;
    }
    //See whether a separate header file exists.
    DTSC::File tmp(config->getString("input") + ".dtsh");
    if (tmp) {
      myMeta = tmp.getMeta();
      return true;
    }
    //parse ismv header
    fseek(inFile, 0, SEEK_SET);
    std::string ftyp;
    readBox("ftyp", ftyp);
    if (ftyp == ""){
      return false;
    }
    std::string boxRes;
    readBox("moov", boxRes);
    if (boxRes == ""){
      return false;
    }
    MP4::MOOV hdrBox;
    hdrBox.read(boxRes);
    parseMoov(hdrBox);
    int tId;
    std::vector<MP4::trunSampleInformation> trunSamples;
    std::vector<std::string> initVecs;
    std::string mdat;
    unsigned int currOffset;
    JSON::Value lastPack;
    unsigned int lastBytePos = 0;
    std::map<int, unsigned int> currentDuration;
    unsigned int curBytePos = ftell(inFile);
    //parse fragments form here
    while (parseFrag(tId, trunSamples, initVecs, mdat)) {
      if (!currentDuration.count(tId)) {
        currentDuration[tId] = 0;
      }
      currOffset = 8;
      int i = 0;
      while (currOffset < mdat.size()) {
        lastPack.null();
        lastPack["time"] = currentDuration[tId] / 10000;
        lastPack["trackid"] = tId;
        lastPack["data"] = mdat.substr(currOffset, trunSamples[i].sampleSize);
        if (initVecs.size() == trunSamples.size()) {
          lastPack["ivec"] = initVecs[i];
        }
        lastPack["duration"] = trunSamples[i].sampleDuration;
        if (myMeta.tracks[tId].type == "video") {
          if (i) {
            lastPack["interframe"] = 1LL;
            lastBytePos ++;
          } else {
            lastPack["keyframe"] = 1LL;
            lastBytePos = curBytePos;
          }
          lastPack["bpos"] = lastBytePos;
          lastPack["nalu"] = 1LL;
          unsigned int offsetConv = trunSamples[i].sampleOffset / 10000;
          lastPack["offset"] = *(int*)&offsetConv;
        } else {
          if (i == 0) {
            lastPack["keyframe"] = 1LL;
            lastPack["bpos"] = curBytePos;
          }
        }
        myMeta.update(lastPack);
        currentDuration[tId] += trunSamples[i].sampleDuration;
        currOffset += trunSamples[i].sampleSize;
        i ++;
      }
      curBytePos = ftell(inFile);
    }
    std::ofstream oFile(std::string(config->getString("input") + ".dtsh").c_str());
    oFile << myMeta.toJSON().toNetPacked();
    oFile.close();
    return true;
  }

  void inputISMV::getNext(bool smart) {
    static JSON::Value thisPack;
    thisPack.null();
    if (!buffered.size()){
      lastPack.null();
      return;
    }
    int tId = buffered.begin()->trackId;
    thisPack["time"] = (long long int)(buffered.begin()->time / 10000);
    thisPack["trackid"] = tId;
    fseek(inFile, buffered.begin()->position, SEEK_SET);
    char * tmpData = (char*)malloc(buffered.begin()->size * sizeof(char));
    fread(tmpData, buffered.begin()->size, 1, inFile);
    thisPack["data"] = std::string(tmpData, buffered.begin()->size);
    free(tmpData);
    if (buffered.begin()->iVec != "") {
      thisPack["ivec"] = buffered.begin()->iVec;
    }
    if (myMeta.tracks[tId].type == "video") {
      if (buffered.begin()->isKeyFrame) {
        thisPack["keyframe"] = 1LL;
      } else {
        thisPack["interframe"] = 1LL;
      }
      thisPack["nalu"] = 1LL;
      thisPack["offset"] = buffered.begin()->offset / 10000;
    } else {
      if (buffered.begin()->isKeyFrame) {
        thisPack["keyframe"] = 1LL;
      }
    }
    thisPack["bpos"] = buffered.begin()->position;
    buffered.erase(buffered.begin());
    if (buffered.size() < 2 * selectedTracks.size()){
      for (std::set<int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
        parseFragHeader(*it, lastKeyNum[*it]);
        lastKeyNum[*it]++;
      }
    }
    std::string tmpStr = thisPack.toNetPacked();
    lastPack.reInit(tmpStr.data(), tmpStr.size());
  }
  
  ///\brief Overloads Input::atKeyFrame, for ISMV always sets the keyframe number
  bool inputISMV::atKeyFrame(){
    return lastPack.getFlag("keyframe");
  }

  void inputISMV::seek(int seekTime) {
    buffered.clear();
    //Seek to corresponding keyframes on EACH track
    for (std::set<int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      unsigned int i;
      for (i = 0; i < myMeta.tracks[*it].keys.size(); i++){
        if (myMeta.tracks[*it].keys[i].getTime() > seekTime && i > 0){//Ehh, whut?
          break;
        }
      }
      i --;
      DEBUG_MSG(DLVL_DEVEL, "ISMV seek frag %d:%d", *it, i);
      parseFragHeader(*it, i);
      lastKeyNum[*it] = i + 1;
    }
  }

  void inputISMV::trackSelect(std::string trackSpec) {
    selectedTracks.clear();
    long long int index;
    while (trackSpec != "") {
      index = trackSpec.find(' ');
      selectedTracks.insert(atoi(trackSpec.substr(0, index).c_str()));
      if (index != std::string::npos) {
        trackSpec.erase(0, index + 1);
      } else {
        trackSpec = "";
      }
    }
    seek(0);
  }

  void inputISMV::parseMoov(MP4::MOOV & moovBox) {
    for (unsigned int i = 0; i < moovBox.getContentCount(); i++) {
      if (moovBox.getContent(i).isType("mvhd")) {
        MP4::MVHD content = (MP4::MVHD &)moovBox.getContent(i);
      }
      if (moovBox.getContent(i).isType("trak")) {
        MP4::TRAK content = (MP4::TRAK &)moovBox.getContent(i);
        int trackId;
        for (unsigned int j = 0; j < content.getContentCount(); j++) {
          if (content.getContent(j).isType("tkhd")) {
            MP4::TKHD subContent = (MP4::TKHD &)content.getContent(j);
            trackId = subContent.getTrackID();
            myMeta.tracks[trackId].trackID = trackId;
          }
          if (content.getContent(j).isType("mdia")) {
            MP4::MDIA subContent = (MP4::MDIA &)content.getContent(j);
            for (unsigned int k = 0; k < subContent.getContentCount(); k++) {
              if (subContent.getContent(k).isType("hdlr")) {
                MP4::HDLR subsubContent = (MP4::HDLR &)subContent.getContent(k);
                if (subsubContent.getHandlerType() == "soun") {
                  myMeta.tracks[trackId].type = "audio";
                }
                if (subsubContent.getHandlerType() == "vide") {
                  myMeta.tracks[trackId].type = "video";
                }
              }
              if (subContent.getContent(k).isType("minf")) {
                MP4::MINF subsubContent = (MP4::MINF &)subContent.getContent(k);
                for (unsigned int l = 0; l < subsubContent.getContentCount(); l++) {
                  if (subsubContent.getContent(l).isType("stbl")) {
                    MP4::STBL stblBox = (MP4::STBL &)subsubContent.getContent(l);
                    for (unsigned int m = 0; m < stblBox.getContentCount(); m++) {
                      if (stblBox.getContent(m).isType("stsd")) {
                        MP4::STSD stsdBox = (MP4::STSD &)stblBox.getContent(m);
                        for (unsigned int n = 0; n < stsdBox.getEntryCount(); n++) {
                          if (stsdBox.getEntry(n).isType("mp4a") || stsdBox.getEntry(n).isType("enca")) {
                            MP4::MP4A mp4aBox = (MP4::MP4A &)stsdBox.getEntry(n);
                            myMeta.tracks[trackId].codec = "AAC";
                            std::string tmpStr;
                            tmpStr += (char)((mp4aBox.toAACInit() & 0xFF00) >> 8);
                            tmpStr += (char)(mp4aBox.toAACInit() & 0x00FF);
                            myMeta.tracks[trackId].init  = tmpStr;
                            myMeta.tracks[trackId].channels = mp4aBox.getChannelCount();
                            myMeta.tracks[trackId].size = mp4aBox.getSampleSize();
                            myMeta.tracks[trackId].rate = mp4aBox.getSampleRate();
                          }
                          if (stsdBox.getEntry(n).isType("avc1") || stsdBox.getEntry(n).isType("encv")) {
                            MP4::AVC1 avc1Box = (MP4::AVC1 &)stsdBox.getEntry(n);
                            myMeta.tracks[trackId].height = avc1Box.getHeight();
                            myMeta.tracks[trackId].width = avc1Box.getWidth();
                            myMeta.tracks[trackId].init = std::string(avc1Box.getCLAP().payload(), avc1Box.getCLAP().payloadSize());
                            myMeta.tracks[trackId].codec = "H264";
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  bool inputISMV::parseFrag(int & tId, std::vector<MP4::trunSampleInformation> & trunSamples, std::vector<std::string> & initVecs, std::string & mdat) {
    tId = -1;
    trunSamples.clear();
    initVecs.clear();
    mdat.clear();
    std::string boxRes;
    readBox("moof", boxRes);
    if (boxRes == ""){
      return false;
    }
    MP4::MOOF moof;
    moof.read(boxRes);
    for (unsigned int i = 0; i < moof.getContentCount(); i++) {
      if (moof.getContent(i).isType("traf")) {
        MP4::TRAF trafBox = (MP4::TRAF &)moof.getContent(i);
        for (unsigned int j = 0; j < trafBox.getContentCount(); j++) {
          if (trafBox.getContent(j).isType("trun")) {
            MP4::TRUN trunBox = (MP4::TRUN &)trafBox.getContent(j);
            for (unsigned int i = 0; i < trunBox.getSampleInformationCount(); i++) {
              trunSamples.push_back(trunBox.getSampleInformation(i));
            }
          }
          if (trafBox.getContent(j).isType("tfhd")) {
            tId = ((MP4::TFHD &)trafBox.getContent(j)).getTrackID();
          }
          /*LTS-START*/
          if (trafBox.getContent(j).isType("uuid")) {
            if (((MP4::UUID &)trafBox.getContent(j)).getUUID() == "a2394f52-5a9b-4f14-a244-6c427c648df4") {
              MP4::UUID_SampleEncryption uuidBox = (MP4::UUID_SampleEncryption &)trafBox.getContent(j);
              for (unsigned int i = 0; i < uuidBox.getSampleCount(); i++) {
                initVecs.push_back(uuidBox.getSample(i).InitializationVector);
              }
            }
          }
          /*LTS-END*/
        }
      }
    }
    readBox("mdat", mdat);
    if (mdat ==""){
      return false;
    }
    return true;
  }

  void inputISMV::parseFragHeader(const unsigned int & trackId, const unsigned int & keyNum) {
    if (!myMeta.tracks.count(trackId) || (myMeta.tracks[trackId].keys.size() <= keyNum)) {
      return;
    }
    long long int lastPos = myMeta.tracks[trackId].keys[keyNum].getBpos();
    long long int lastTime = myMeta.tracks[trackId].keys[keyNum].getTime() * 10000;
    fseek(inFile, lastPos, SEEK_SET);
    std::string boxRes;
    readBox("moof", boxRes);
    if (boxRes == ""){
      return;
    }
    MP4::MOOF moof;
    moof.read(boxRes);
    MP4::TRUN trunBox;
    MP4::UUID_SampleEncryption uuidBox; /*LTS*/
    for (unsigned int i = 0; i < moof.getContentCount(); i++) {
      if (moof.getContent(i).isType("traf")) {
        MP4::TRAF trafBox = (MP4::TRAF &)moof.getContent(i);
        for (unsigned int j = 0; j < trafBox.getContentCount(); j++) {
          if (trafBox.getContent(j).isType("trun")) {
            trunBox = (MP4::TRUN &)trafBox.getContent(j);
          }
          if (trafBox.getContent(j).isType("tfhd")) {
            if (trackId != ((MP4::TFHD &)trafBox.getContent(j)).getTrackID()){
              DEBUG_MSG(DLVL_FAIL,"Trackids do not match");
              return;
            }
          }
          /*LTS-START*/
          if (trafBox.getContent(j).isType("uuid")) {
            if (((MP4::UUID &)trafBox.getContent(j)).getUUID() == "a2394f52-5a9b-4f14-a244-6c427c648df4") {
              uuidBox = (MP4::UUID_SampleEncryption &)trafBox.getContent(j);
            }
          }
          /*LTS-END*/
        }
      }
    }
    lastPos = ftell(inFile) + 8;
    for (unsigned int i = 0; i < trunBox.getSampleInformationCount(); i++){
      seekPos myPos;
      myPos.position = lastPos;
      myPos.trackId = trackId;
      myPos.time = lastTime;
      myPos.duration = trunBox.getSampleInformation(i).sampleDuration;
      myPos.size = trunBox.getSampleInformation(i).sampleSize;
      if( trunBox.getFlags() & MP4::trunsampleOffsets){
        unsigned int offsetConv = trunBox.getSampleInformation(i).sampleOffset;
        myPos.offset = *(int*)&offsetConv;
      }else{
        myPos.offset = 0;
      }
      myPos.isKeyFrame = (i == 0);
      /*LTS-START*/
      if (i <= uuidBox.getSampleCount()){
        myPos.iVec = uuidBox.getSample(i).InitializationVector;
      }
      /*LTS-END*/
      lastTime += trunBox.getSampleInformation(i).sampleDuration;
      lastPos += trunBox.getSampleInformation(i).sampleSize;
      buffered.insert(myPos);
    }
  }

  void inputISMV::readBox(const char * type, std::string & result) {
    int pos = ftell(inFile);
    char mp4Head[8];
    fread(mp4Head, 8, 1, inFile);
    fseek(inFile, pos, SEEK_SET);
    if (memcmp(mp4Head + 4, type, 4)) {
      DEBUG_MSG(DLVL_FAIL, "No %.4s box found at position %d", type, pos);
      result = "";
      return;
    }
    unsigned int boxSize = (mp4Head[0] << 24) + (mp4Head[1] << 16) + (mp4Head[2] << 8) + mp4Head[3];
    char * tmpBox = (char *)malloc(boxSize * sizeof(char));
    fread(tmpBox, boxSize, 1, inFile);
    result = std::string(tmpBox, boxSize);
    free(tmpBox);
  }
}





