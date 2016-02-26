#include <iostream>
#include <fstream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mist/stream.h>
#include <mist/flv_tag.h>
#include <mist/defines.h>

#include "input_mp4.h"

namespace Mist {

  mp4TrackHeader::mp4TrackHeader(){
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
  }

  long unsigned int mp4TrackHeader::size(){
    if (!stszBox.asBox()){
      return 0;
    }else{
      return stszBox.getSampleCount();
    }
  }

  void mp4TrackHeader::read(MP4::TRAK & trakBox){
    initialised = false;
    std::string tmp;//temporary string for copying box data
    MP4::Box trakLoopPeek;
    timeScale = 1;
    //for all in trak
    for (uint32_t j = 0; j < trakBox.getContentCount(); j++){
      trakLoopPeek = MP4::Box(trakBox.getContent(j).asBox(),false);
      std::string trakBoxType = trakLoopPeek.getType();
      if (trakBoxType == "mdia"){//fi tkhd & if mdia
        MP4::Box mdiaLoopPeek;
        //for all in mdia
        for (uint32_t k = 0; k < ((MP4::MDIA&)trakLoopPeek).getContentCount(); k++){
          mdiaLoopPeek = MP4::Box(((MP4::MDIA&)trakLoopPeek).getContent(k).asBox(),false);
          std::string mdiaBoxType = mdiaLoopPeek.getType();
          if (mdiaBoxType == "mdhd"){
            timeScale = ((MP4::MDHD&)mdiaLoopPeek).getTimeScale();
          }else if (mdiaBoxType == "minf"){//fi hdlr
            //for all in minf
            //get all boxes: stco stsz,stss
            MP4::Box minfLoopPeek;
            for (uint32_t l = 0; l < ((MP4::MINF&)mdiaLoopPeek).getContentCount(); l++){
              minfLoopPeek = MP4::Box(((MP4::MINF&)mdiaLoopPeek).getContent(l).asBox(),false);
              std::string minfBoxType = minfLoopPeek.getType();
              ///\todo more stuff here
              if (minfBoxType == "stbl"){
                MP4::Box stblLoopPeek;
                for (uint32_t m = 0; m < ((MP4::STBL&)minfLoopPeek).getContentCount(); m++){
                  stblLoopPeek = MP4::Box(((MP4::STBL&)minfLoopPeek).getContent(m).asBox(),false);
                  std::string stblBoxType = stblLoopPeek.getType();
                  if (stblBoxType == "stts"){
                    tmp = std::string(stblLoopPeek.asBox() ,stblLoopPeek.boxedSize());
                    sttsBox.clear();
                    sttsBox.read(tmp);
                  }else if (stblBoxType == "ctts"){
                    ///\todo this box should not have to be read, since its information is taken from the DTSH
                    tmp = std::string(stblLoopPeek.asBox() ,stblLoopPeek.boxedSize());
                    cttsBox.clear();
                    cttsBox.read(tmp);
                    hasCTTS = true;
                  }else if (stblBoxType == "stsz"){
                    tmp = std::string(stblLoopPeek.asBox() ,stblLoopPeek.boxedSize());
                    stszBox.clear();
                    stszBox.read(tmp);
                  }else if (stblBoxType == "stco" || stblBoxType == "co64"){
                    tmp = std::string(stblLoopPeek.asBox() ,stblLoopPeek.boxedSize());
                    stcoBox.clear();
                    stcoBox.read(tmp);
                  }else if (stblBoxType == "stsc"){
                    tmp = std::string(stblLoopPeek.asBox() ,stblLoopPeek.boxedSize());
                    stscBox.clear();
                    stscBox.read(tmp);
                  }
                }//rof stbl
              }//fi stbl
            }//rof minf
          }//fi minf
        }//rof mdia
      }//fi mdia
    }//rof trak
  }

  void mp4TrackHeader::getPart(long unsigned int index, long long unsigned int & offset,unsigned int& size, long long unsigned int & timestamp, long long unsigned int & timeOffset){


    if (index < sampleIndex){
      sampleIndex = 0;
      stscStart = 0;
    }
    
    while (stscStart < stscBox.getEntryCount()){
      //check where the next index starts
      unsigned long long nextSampleIndex;
      if (stscStart + 1 < stscBox.getEntryCount()){
        nextSampleIndex = sampleIndex + (stscBox.getSTSCEntry(stscStart+1).firstChunk - stscBox.getSTSCEntry(stscStart).firstChunk) * stscBox.getSTSCEntry(stscStart).samplesPerChunk;
      }else{
        nextSampleIndex = stszBox.getSampleCount();
      }
      //if the next one is too far, we're in the right spot
      if (nextSampleIndex > index){
        break;
      }
      sampleIndex = nextSampleIndex;
      ++stscStart;
    }

    if (sampleIndex > index){
      FAIL_MSG("Could not complete seek - not in file (%llu > %lu)", sampleIndex, index);
    }

    long long unsigned stcoPlace = (stscBox.getSTSCEntry(stscStart).firstChunk - 1 ) + ((index - sampleIndex) / stscBox.getSTSCEntry(stscStart).samplesPerChunk);
    long long unsigned stszStart = sampleIndex + (stcoPlace - (stscBox.getSTSCEntry(stscStart).firstChunk - 1)) * stscBox.getSTSCEntry(stscStart).samplesPerChunk;

    //set the offset to the correct value
    if (stcoBox.getType() == "co64"){
      offset = ((MP4::CO64*)&stcoBox)->getChunkOffset(stcoPlace);
    }else{
      offset = stcoBox.getChunkOffset(stcoPlace);
    }

    for (int j = stszStart; j < index; j++){
      offset += stszBox.getEntrySize(j);
    }

    if (index < deltaPos){
      deltaIndex = 0;
      deltaPos = 0;
      deltaTotal = 0;
    }

    MP4::STTSEntry tmpSTTS;
    while (deltaIndex < sttsBox.getEntryCount()){
      tmpSTTS = sttsBox.getSTTSEntry(deltaIndex);
      if ((index - deltaPos) < tmpSTTS.sampleCount){
        break;
      }else{
        deltaTotal += tmpSTTS.sampleCount * tmpSTTS.sampleDelta;
        deltaPos += tmpSTTS.sampleCount;
      }
      ++deltaIndex;
    }
    timestamp = ((deltaTotal + ((index-deltaPos) * tmpSTTS.sampleDelta))*1000) / timeScale;
    initialised = true;

    if (index < offsetPos){
      offsetIndex = 0;
      offsetPos = 0;
    }
    if (hasCTTS){
      MP4::CTTSEntry tmpCTTS;
      while (offsetIndex < cttsBox.getEntryCount()){
        tmpCTTS = cttsBox.getCTTSEntry(offsetIndex);
        if ((index - offsetPos) < tmpCTTS.sampleCount){
          timeOffset = (tmpCTTS.sampleOffset*1000)/timeScale;
          break;
        }
        offsetPos += tmpCTTS.sampleCount;
        ++offsetIndex;
      }
    }

    //next lines are common for next-getting and seeking
    size = stszBox.getEntrySize(index);

  }
  
  inputMP4::inputMP4(Util::Config * cfg) : Input(cfg) {
    malSize = 4;//initialise data read buffer to 0;
    data = (char*)malloc(malSize);
    capa["name"] = "MP4";
    capa["decs"] = "Enables MP4 Input";
    capa["source_match"] = "/*.mp4";
    capa["priority"] = 9ll;
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("H263");
    capa["codecs"][0u][0u].append("VP6");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("AC3");
    capa["codecs"][0u][1u].append("MP3");
  }
  
  inputMP4::~inputMP4(){
    free(data);
  }
  
  bool inputMP4::setup() {
    if (config->getString("input") == "-") {
      std::cerr << "Input from stdin not yet supported" << std::endl;
      return false;
    }
    if (!config->getString("streamname").size()){
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

  bool inputMP4::readHeader() {
    if (!inFile) {
      INFO_MSG("inFile failed!");
      return false;
    }
    //make trackmap here from inFile
    long long unsigned int trackNo = 0;
    
    std::string tmp;//temp string for reading boxes.

    
    //first we get the necessary header parts
    while(!feof(inFile)){
      std::string boxType = MP4::readBoxType(inFile);
      if (boxType=="moov"){
        MP4::MOOV moovBox;
        moovBox.read(inFile);
        //for all box in moov
        
        MP4::Box moovLoopPeek;
        for (uint32_t i = 0; i < moovBox.getContentCount(); i++){
          tmp = std::string(moovBox.getContent(i).asBox() ,moovBox.getContent(i).boxedSize());
          moovLoopPeek.read(tmp);
          //if trak
          if (moovLoopPeek.getType() == "trak"){
            //create new track entry here
            trackNo ++;
            
            headerData[trackNo].read((MP4::TRAK&)moovLoopPeek);
          }
        }
      }else if (boxType == "erro"){
        break;
      }else{
        if (!MP4::skipBox(inFile)){//moving on to next box
          DEBUG_MSG(DLVL_FAIL,"Error in skipping box, exiting");
          return false;
        }
      }//fi moov
    }//when at the end of the file
    //seek file to 0;
    fseeko(inFile,0,SEEK_SET);
    
    //See whether a separate header file exists.
    DTSC::File tmpdtsh(config->getString("input") + ".dtsh");
    if (tmpdtsh){
      myMeta = tmpdtsh.getMeta();
      return true;
    }
    trackNo = 0;
    //Create header file from MP4 data
    while(!feof(inFile)){
      std::string boxType = MP4::readBoxType(inFile);
      if (boxType=="moov"){
        MP4::MOOV moovBox;
        moovBox.read(inFile);
        //for all box in moov
        MP4::Box moovLoopPeek;
        for (uint32_t i = 0; i < moovBox.getContentCount(); i++){
          tmp = std::string(moovBox.getContent(i).asBox(), moovBox.getContent(i).boxedSize());
          moovLoopPeek.read(tmp);
          //if trak
          if (moovLoopPeek.getType() == "trak"){
            //create new track entry here
            long long unsigned int trackNo = myMeta.tracks.size()+1;
            myMeta.tracks[trackNo].trackID = trackNo;
            MP4::Box trakLoopPeek;
            unsigned long int timeScale = 1;
            //for all in trak
            for (uint32_t j = 0; j < ((MP4::TRAK&)moovLoopPeek).getContentCount(); j++){
              tmp = std::string(((MP4::MOOV&)moovLoopPeek).getContent(j).asBox(),((MP4::MOOV&)moovLoopPeek).getContent(j).boxedSize());
              trakLoopPeek.read(tmp);
              std::string trakBoxType = trakLoopPeek.getType();
              //note: per track: trackID codec, type (vid/aud), init
              //if tkhd
              if (trakBoxType == "tkhd"){
                MP4::TKHD tkhdBox(0,0,0,0);///\todo: this can be done with casting
                tmp = std::string(trakLoopPeek.asBox(), trakLoopPeek.boxedSize());
                tkhdBox.read(tmp);
                //remember stuff for decoding stuff later
                if (tkhdBox.getWidth() > 0){
                  myMeta.tracks[trackNo].width = tkhdBox.getWidth();
                  myMeta.tracks[trackNo].height = tkhdBox.getHeight();
                }
              }else if (trakBoxType == "mdia"){//fi tkhd & if mdia
                MP4::Box mdiaLoopPeek;
                //for all in mdia
                for (uint32_t k = 0; k < ((MP4::MDIA&)trakLoopPeek).getContentCount(); k++){
                  tmp = std::string(((MP4::MDIA&)trakLoopPeek).getContent(k).asBox(),((MP4::MDIA&)trakLoopPeek).getContent(k).boxedSize());
                  mdiaLoopPeek.read(tmp);
                  std::string mdiaBoxType = mdiaLoopPeek.getType();
                  if (mdiaBoxType == "mdhd"){
                    timeScale = ((MP4::MDHD&)mdiaLoopPeek).getTimeScale();
                  }else if (mdiaBoxType == "hdlr"){//fi mdhd
                    std::string handlerType = ((MP4::HDLR&)mdiaLoopPeek).getHandlerType();
                    if (handlerType != "vide" && handlerType !="soun"){
                      myMeta.tracks.erase(trackNo);
                      //skip meta boxes for now
                      break;
                    }
                  }else if (mdiaBoxType == "minf"){//fi hdlr
                    //for all in minf
                    //get all boxes: stco stsz,stss
                    MP4::Box minfLoopPeek;
                    for (uint32_t l = 0; l < ((MP4::MINF&)mdiaLoopPeek).getContentCount(); l++){
                      tmp = std::string(((MP4::MINF&)mdiaLoopPeek).getContent(l).asBox(),((MP4::MINF&)mdiaLoopPeek).getContent(l).boxedSize());
                      minfLoopPeek.read(tmp);
                      std::string minfBoxType = minfLoopPeek.getType();
                      ///\todo more stuff here
                      if (minfBoxType == "stbl"){
                        MP4::Box stblLoopPeek;
                        MP4::STSS stssBox;
                        MP4::STTS sttsBox;
                        MP4::STSZ stszBox;
                        MP4::STCO stcoBox;
                        MP4::STSC stscBox;
                        MP4::CTTS cttsBox;//optional ctts box
                        bool hasCTTS = false;
                        for (uint32_t m = 0; m < ((MP4::STBL&)minfLoopPeek).getContentCount(); m++){
                          tmp = std::string(((MP4::STBL&)minfLoopPeek).getContent(m).asBox(),((MP4::STBL&)minfLoopPeek).getContent(m).boxedSize());
                          std::string stboxRead = tmp;
                          stblLoopPeek.read(tmp);
                          std::string stblBoxType = stblLoopPeek.getType();
                          if (stblBoxType == "stss"){
                            stssBox.read(stboxRead);
                          }else if (stblBoxType == "stts"){
                            sttsBox.read(stboxRead);
                          }else if (stblBoxType == "stsz"){
                            stszBox.read(stboxRead);
                          }else if (stblBoxType == "stco" || stblBoxType == "co64"){
                            stcoBox.read(stboxRead);
                          }else if (stblBoxType == "stsc"){
                            stscBox.read(stboxRead);
                          }else if (stblBoxType == "ctts"){
                            cttsBox.read(stboxRead);
                            hasCTTS = true;
                          }else if (stblBoxType == "stsd"){
                            //check for codec in here
                            MP4::Box & tmpBox = ((MP4::STSD&)stblLoopPeek).getEntry(0);
                            std::string tmpType = tmpBox.getType();
                            INFO_MSG("Found track of type %s", tmpType.c_str()); 
                            if (tmpType == "avc1" || tmpType == "h264" || tmpType == "mp4v"){
                              myMeta.tracks[trackNo].type = "video";
                              myMeta.tracks[trackNo].codec = "H264";
                              if (!myMeta.tracks[trackNo].width){
                                myMeta.tracks[trackNo].width = ((MP4::VisualSampleEntry&)tmpBox).getWidth();
                                myMeta.tracks[trackNo].height = ((MP4::VisualSampleEntry&)tmpBox).getHeight();
                              }
                              MP4::Box tmpBox2 = tmpBox;
                              MP4::Box tmpContent = ((MP4::VisualSampleEntry&)tmpBox2).getCLAP();
                              if (tmpContent.getType() == "avcC"){
                                myMeta.tracks[trackNo].init = std::string(tmpContent.payload(),tmpContent.payloadSize());
                              }
                              tmpContent = ((MP4::VisualSampleEntry&)tmpBox2).getPASP();
                              if (tmpContent.getType() == "avcC"){
                                myMeta.tracks[trackNo].init = std::string(tmpContent.payload(),tmpContent.payloadSize());
                              }
                            }else if (tmpType == "hev1" || tmpType == "hvc1"){
                              myMeta.tracks[trackNo].type = "video";
                              myMeta.tracks[trackNo].codec = "HEVC";
                              if (!myMeta.tracks[trackNo].width){
                                myMeta.tracks[trackNo].width = ((MP4::VisualSampleEntry&)tmpBox).getWidth();
                                myMeta.tracks[trackNo].height = ((MP4::VisualSampleEntry&)tmpBox).getHeight();
                              }
                              MP4::Box tmpBox2 = tmpBox;
                              MP4::Box tmpContent = ((MP4::VisualSampleEntry&)tmpBox2).getCLAP();
                              if (tmpContent.getType() == "hvcC"){
                                myMeta.tracks[trackNo].init = std::string(tmpContent.payload(),tmpContent.payloadSize());
                              }
                              tmpContent = ((MP4::VisualSampleEntry&)tmpBox2).getPASP();
                              if (tmpContent.getType() == "hvcC"){
                                myMeta.tracks[trackNo].init = std::string(tmpContent.payload(),tmpContent.payloadSize());
                              }
                            }else if (tmpType == "mp4a" || tmpType == "aac " || tmpType == "ac-3"){
                              myMeta.tracks[trackNo].type = "audio";
                              myMeta.tracks[trackNo].channels = ((MP4::AudioSampleEntry&)tmpBox).getChannelCount();
                              myMeta.tracks[trackNo].rate = (long long int)(((MP4::AudioSampleEntry&)tmpBox).getSampleRate());
                              if (tmpType == "ac-3"){
                                myMeta.tracks[trackNo].codec = "AC3";
                              }else{
                                MP4::Box esds = ((MP4::AudioSampleEntry&)tmpBox).getCodecBox();
                                myMeta.tracks[trackNo].codec = ((MP4::ESDS&)esds).getCodec();
                                myMeta.tracks[trackNo].init = ((MP4::ESDS&)esds).getInitData();
                              }
                              myMeta.tracks[trackNo].size = 16;///\todo this might be nice to calculate from mp4 file;
                              //get Visual sample entry -> esds -> startcodes
                            }else{
                              myMeta.tracks.erase(trackNo);
                            }
                            
                          }
                        }//rof stbl
                        uint64_t totaldur = 0;///\todo note: set this to begin time
                        mp4PartBpos BsetPart;
                        long long unsigned int entryNo = 0;
                        long long unsigned int sampleNo = 0;
                        MP4::STTSEntry tempSTTS;
                        tempSTTS = sttsBox.getSTTSEntry(entryNo);
                        long long unsigned int curSTSS = 0;
                        bool vidTrack = (myMeta.tracks[trackNo].type == "video");
                        //change to for over all samples
                        unsigned int stcoIndex = 0;
                        unsigned int stscIndex = 0;
                        unsigned int cttsIndex = 0;//current ctts Index we are reading
                        unsigned int cttsEntryRead = 0;//current part of ctts we are reading
                        MP4::CTTSEntry cttsEntry;
                        
                        unsigned int fromSTCOinSTSC = 0;
                        long long unsigned int tempOffset;
                        bool stcoIs64 = (stcoBox.getType() == "co64");
                        if (stcoIs64){
                          tempOffset = ((MP4::CO64*)&stcoBox)->getChunkOffset(0);
                        }else{
                          tempOffset = stcoBox.getChunkOffset(0);
                        }
                        long long unsigned int nextFirstChunk;
                        if (stscBox.getEntryCount() > 1){
                          nextFirstChunk = stscBox.getSTSCEntry(1).firstChunk - 1;
                        }else{
                          if (stcoIs64){
                            nextFirstChunk = ((MP4::CO64*)&stcoBox)->getEntryCount();
                          }else{
                            nextFirstChunk = stcoBox.getEntryCount();
                          }
                        }
                        for(long long unsigned int sampleIndex = 0; sampleIndex < stszBox.getSampleCount(); sampleIndex ++){
                          if (stcoIndex >= nextFirstChunk){//note
                            stscIndex ++;
                            if (stscIndex + 1 < stscBox.getEntryCount()){
                              nextFirstChunk = stscBox.getSTSCEntry(stscIndex + 1).firstChunk - 1;
                            }else{
                              if (stcoIs64){
                                nextFirstChunk = ((MP4::CO64*)&stcoBox)->getEntryCount();
                              }else{
                                nextFirstChunk = stcoBox.getEntryCount();
                              }
                            }
                          }
                          if (vidTrack && curSTSS < stssBox.getEntryCount() && sampleIndex + 1 == stssBox.getSampleNumber(curSTSS)){
                            BsetPart.keyframe = true;
                            curSTSS ++;
                          }else{
                            BsetPart.keyframe = false;
                          }
                          //in bpos set
                          BsetPart.stcoNr=stcoIndex;
                          //bpos = chunkoffset[samplenr] in stco
                          BsetPart.bpos = tempOffset;
                          fromSTCOinSTSC ++;
                          if (fromSTCOinSTSC < stscBox.getSTSCEntry(stscIndex).samplesPerChunk){//as long as we are still in this chunk
                            tempOffset += stszBox.getEntrySize(sampleIndex);
                          }else{
                            stcoIndex ++;
                            fromSTCOinSTSC = 0;
                            if (stcoIs64){
                              tempOffset = ((MP4::CO64*)&stcoBox)->getChunkOffset(stcoIndex);
                            }else{
                              tempOffset = stcoBox.getChunkOffset(stcoIndex);
                            }
                          }
                          //time = totaldur + stts[entry][sample]
                          BsetPart.time = (totaldur*1000)/timeScale;
                          totaldur += tempSTTS.sampleDelta;
                          sampleNo++;
                          if (sampleNo >= tempSTTS.sampleCount){
                            entryNo++;
                            sampleNo = 0;
                            if (entryNo < sttsBox.getEntryCount()){
                              tempSTTS = sttsBox.getSTTSEntry(entryNo);
                            }
                          }
                          //set time offset
                          if (hasCTTS){
                            
                            cttsEntry = cttsBox.getCTTSEntry(cttsIndex);
                            cttsEntryRead++;
                            if (cttsEntryRead >= cttsEntry.sampleCount){
                              cttsIndex++;
                              cttsEntryRead = 0;
                            }
                            BsetPart.timeOffset = (cttsEntry.sampleOffset * 1000)/timeScale;
                          }else{
                            BsetPart.timeOffset = 0;
                          }
                          myMeta.update(BsetPart.time, BsetPart.timeOffset, trackNo, stszBox.getEntrySize(sampleIndex), BsetPart.bpos, BsetPart.keyframe);
                        }//while over stsc
                        if (vidTrack){
                          //something wrong with the time formula, but the answer is right for some reason
                          /// \todo Fix this. This makes no sense whatsoever. This isn't frame per kilosecond, but milli-STCO-entries per second.
                          // (A single STCO entry may be more than 1 part, and 1 part may be a partial frame or multiple frames)
                          if (stcoIs64){
                            myMeta.tracks[trackNo].fpks = (((double)(((MP4::CO64*)&stcoBox)->getEntryCount()*1000))/((totaldur*1000)/timeScale))*1000;
                          }else{
                            myMeta.tracks[trackNo].fpks = (((double)(stcoBox.getEntryCount()*1000))/((totaldur*1000)/timeScale))*1000;
                          }
                        }
                      }//fi stbl
                    }//rof minf
                  }//fi minf
                }//rof mdia
              }//fi mdia
            }//rof trak
          }//endif trak
        }//rof moov
      }else if (boxType == "erro"){
        break;
      }else{
        if (!MP4::skipBox(inFile)){//moving on to next box
          DEBUG_MSG(DLVL_FAIL,"Error in Skipping box, exiting");
          return false;
        }
      }// if moov
    }// end while file read
    //for all in bpos set, find its data
    clearerr(inFile);
    
    //outputting dtsh file
    std::ofstream oFile(std::string(config->getString("input") + ".dtsh").c_str());
    oFile << myMeta.toJSON().toNetPacked();
    oFile.close();
    return true;
  }
  
  void inputMP4::getNext(bool smart) {//get next part from track in stream
    if (curPositions.empty()){
      thisPacket.null();
      return;
    }
    //pop uit set
    mp4PartTime curPart = *curPositions.begin();
    curPositions.erase(curPositions.begin());
    
    bool isKeyframe = false;
    if(nextKeyframe[curPart.trackID] < myMeta.tracks[curPart.trackID].keys.size()){
      //checking if this is a keyframe
      if (myMeta.tracks[curPart.trackID].type == "video" && (long long int) curPart.time == myMeta.tracks[curPart.trackID].keys[(nextKeyframe[curPart.trackID])].getTime()){
        isKeyframe = true;
      }
      //if a keyframe has passed, we find the next keyframe
      if (myMeta.tracks[curPart.trackID].keys[(nextKeyframe[curPart.trackID])].getTime() <= (long long int)curPart.time){
        nextKeyframe[curPart.trackID] ++;
      }
    }
    if (fseeko(inFile,curPart.bpos,SEEK_SET)){
      DEBUG_MSG(DLVL_FAIL,"seek unsuccessful; bpos: %llu error: %s",curPart.bpos, strerror(errno));
      thisPacket.null();
      return;
    }
    if (curPart.size > malSize){
      data = (char*)realloc(data, curPart.size);
      malSize = curPart.size;
    }
    if (fread(data, curPart.size, 1, inFile)!=1){
      DEBUG_MSG(DLVL_FAIL,"read unsuccessful at %ld", ftell(inFile));
      thisPacket.null();
      return;
    }
    thisPacket.genericFill(curPart.time, curPart.offset, curPart.trackID, data, curPart.size, 0/*Note: no bpos*/, isKeyframe);
    
    //get the next part for this track
    curPart.index ++;
    if (curPart.index < headerData[curPart.trackID].size()){
      headerData[curPart.trackID].getPart(curPart.index, curPart.bpos, curPart.size, curPart.time, curPart.offset);
      curPositions.insert(curPart);
    }
  }

  void inputMP4::seek(int seekTime) {//seek to a point
    nextKeyframe.clear();
    //for all tracks
    curPositions.clear();
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      nextKeyframe[*it] = 0;
      mp4PartTime addPart;
      addPart.bpos = 0;
      addPart.size = 0;
      addPart.time = 0;
      addPart.trackID = *it;
      //for all indexes in those tracks
      for (unsigned int i = 0; i < headerData[*it].size(); i++){
        //if time > seekTime
        headerData[*it].getPart(i, addPart.bpos, addPart.size, addPart.time, addPart.offset);
        //check for keyframe time in myMeta and update nextKeyframe
        //
        if (myMeta.tracks[*it].keys[(nextKeyframe[*it])].getTime() < addPart.time){
          nextKeyframe[*it] ++;
        }
        if (addPart.time >= seekTime){
          addPart.index = i;
          //use addPart thingy in time set and break
          curPositions.insert(addPart);
          break;
        }//end if time > seektime
      }//end for all indexes
    }//rof all tracks
  }

  void inputMP4::trackSelect(std::string trackSpec) {
    selectedTracks.clear();
    long long int index;
    while (trackSpec != "") {
      index = trackSpec.find(' ');
      selectedTracks.insert(atoi(trackSpec.substr(0, index).c_str()));
      VERYHIGH_MSG("Added track %d, index = %lld, (index == npos) = %d", atoi(trackSpec.substr(0, index).c_str()), index, index == std::string::npos);
      if (index != std::string::npos) {
        trackSpec.erase(0, index + 1);
      } else {
        trackSpec = "";
      }
    }
  }
}

