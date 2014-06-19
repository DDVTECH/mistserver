#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mist/stream.h>
#include <mist/flv_tag.h>
#include <mist/defines.h>
#include <mist/ts_packet.h>
#include <mist/mp4_generic.h>
#include "input_ts.h"

namespace Mist {
  
  
  /// Constructor of TS Input
  /// \arg cfg Util::Config that contains all current configurations.
  inputTS::inputTS(Util::Config * cfg) : Input(cfg) {
    capa["decs"] = "Enables TS Input";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
  }
  
  ///Setup of TS Input
  bool inputTS::setup() {
    if (config->getString("input") == "-") {
      inFile = stdin;
    }else{
      inFile = fopen(config->getString("input").c_str(), "r");
    }
    
    if (config->getString("output") != "-") {
      std::cerr << "Output to non-stdout not yet supported" << std::endl;
    }
    if (!inFile) {
      return false;
    }
    return true;
  }
  
  ///Track selector of TS Input
  ///\arg trackSpec specifies which tracks  are to be selected
  ///\todo test whether selecting a subset of tracks work
  void inputTS::trackSelect(std::string trackSpec) {
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
  }

  void inputTS::parsePESHeader(int tid, pesBuffer & buf){
    if (buf.data.size() < 9){
      return;
    }
    if (buf.data.size() < 9 + buf.data[8]){
      return;
    }
    if( (((int)buf.data[0] << 16) | ((int)buf.data[1] << 8) | buf.data[2])  != 0x000001){
      DEBUG_MSG(DLVL_WARN, "Parsing PES for track %d failed due to incorrect header (%0.6X), throwing away", tid, (((int)buf.data[0] << 16) | ((int)buf.data[1] << 8) | buf.data[2]) );
      buf.data = "";
      return;
    }
    buf.len = (((int)buf.data[4] << 8) | buf.data[5]) - (3 + buf.data[8]);
    if ((buf.data[7] >> 6) & 0x02){//Check for PTS presence
      buf.time = ((buf.data[9] >> 1) & 0x07);
      buf.time <<= 15;
      buf.time |= ((int)buf.data[10] << 7) | ((buf.data[11] >> 1) & 0x7F);
      buf.time <<= 15;
      buf.time |= ((int)buf.data[12] << 7) | ((buf.data[13] >> 1) & 0x7F);
      buf.time /= 90;
      if (((buf.data[7] & 0xC0) >> 6) & 0x01){//Check for DTS presence (yes, only if PTS present)
        buf.offset = buf.time;
        buf.time = ((buf.data[14] >> 1) & 0x07);
        buf.time <<= 15;
        buf.time |= ((int)buf.data[15] << 7) | ((buf.data[16] >> 1) & 0x7F);
        buf.time <<= 15;
        buf.time |= ((int)buf.data[17] << 7) | ((buf.data[18] >> 1) & 0x7F);
        buf.time /= 90;
        buf.offset -= buf.time;
      }
    }
    if (!firstTimes.count(tid)){
      firstTimes[tid] = buf.time;
    }
    buf.time -= firstTimes[tid];
    buf.data.erase(0, 9 + buf.data[8]);
  }

  void inputTS::parsePESPayload(int tid, pesBuffer & buf){
    if (myMeta.tracks[tid].codec == "H264"){
      parseH264PES(tid, buf);
    }
    if (myMeta.tracks[tid].codec == "AAC"){
      parseAACPES(tid, buf);
    }
  }


  void inputTS::parseAACPES(int tid, pesBuffer & buf){
    if (!buf.data.size()){
      buf.len = 0;
      return;
    }
    if (myMeta.tracks[tid].init == ""){
      char audioInit[2];//5 bits object type, 4 bits frequency index, 4 bits channel index
      char AACProfile = ((buf.data[2] >> 6) & 0x03) + 1;
      char frequencyIndex = ((buf.data[2] >> 2) & 0x0F);
      char channelConfig = ((buf.data[2] & 0x01) << 2) | ((buf.data[3] >> 6) & 0x03);
      switch(frequencyIndex){
        case 0:  myMeta.tracks[tid].rate = 96000; break;
        case 1:  myMeta.tracks[tid].rate = 88200; break;
        case 2:  myMeta.tracks[tid].rate = 64000; break;
        case 3:  myMeta.tracks[tid].rate = 48000; break;
        case 4:  myMeta.tracks[tid].rate = 44100; break;
        case 5:  myMeta.tracks[tid].rate = 32000; break;
        case 6:  myMeta.tracks[tid].rate = 24000; break;
        case 7:  myMeta.tracks[tid].rate = 22050; break;
        case 8:  myMeta.tracks[tid].rate = 16000; break;
        case 9:  myMeta.tracks[tid].rate = 12000; break;
        case 10: myMeta.tracks[tid].rate = 11025; break;
        case 11: myMeta.tracks[tid].rate =  8000; break;
        case 12: myMeta.tracks[tid].rate =  7350; break;
        default: myMeta.tracks[tid].rate =     0; break;
      }
      myMeta.tracks[tid].channels = channelConfig;
      if (channelConfig == 7){
        myMeta.tracks[tid].channels = 8;
      }
      audioInit[0] = ((AACProfile & 0x1F) << 3) | ((frequencyIndex & 0x0E) >> 1);
      audioInit[1] = ((frequencyIndex & 0x01) << 7) | ((channelConfig & 0x0F) << 3);
      myMeta.tracks[tid].init = std::string(audioInit, 2);
      //\todo This value is right now hardcoded, maybe fix this when we know how
      myMeta.tracks[tid].size = 16;
    }
    buf.len = (((buf.data[3] & 0x03) << 11) | (buf.data[4] << 3) | ((buf.data[5] >> 5) & 0x07)) - (buf.data[1] & 0x01 ? 7 :9);
    buf.curSampleCount += 1024 * ((buf.data[6] & 0x3) + 1);//Number of frames * samples per frame(1024);
    buf.data.erase(0, (buf.data[1] & 0x01 ? 7 : 9));//Substract header
  }

  void inputTS::parseH264PES(int tid, pesBuffer & buf){
    static char annexB[] = {0x00,0x00,0x01};
    static char nalLen[4];

    int nalLength = 0;
    std::string newData;
    int pos = 0;
    int nxtPos = buf.data.find(annexB, pos, 3);
    //Rewrite buf.data from annexB to size-prefixed h.264
    while (nxtPos != std::string::npos){
      //Detect next packet (if any) and deduce current packet length
      pos = nxtPos + 3;
      nxtPos = buf.data.find(annexB, pos, 3);
      if (nxtPos == std::string::npos){
        nalLength = buf.data.size() - pos;
      }else{
        nalLength = nxtPos - pos;
        if (buf.data[nxtPos - 1] == 0x00){//4-byte annexB header
          nalLength--;
        }
      }
      //Do nal type specific stuff
      switch (buf.data[pos] & 0x1F){
        case 0x05: buf.isKey = true; break;
        case 0x07: buf.sps = buf.data.substr(pos, nalLength); break;
        case 0x08: buf.pps = buf.data.substr(pos, nalLength); break;
        default: break;
      }
      if ((buf.data[pos] & 0x1F) != 0x07 && (buf.data[pos] & 0x1F) != 0x08 && (buf.data[pos] & 0x1F) != 0x09){
        //Append length + payload
        nalLen[0] = (nalLength >> 24) & 0xFF;
        nalLen[1] = (nalLength >> 16) & 0xFF;
        nalLen[2] = (nalLength >> 8) & 0xFF;
        nalLen[3] = nalLength & 0xFF;
        newData.append(nalLen, 4);
        newData += buf.data.substr(pos, nalLength);
      }
    }
    buf.data = newData;
    buf.len = newData.size();
    //If this packet had both a Sequence Parameter Set (sps) and a Picture Parameter Set (pps), calculate the metadata for the stream
    if (buf.sps != "" && buf.pps != ""){
      MP4::AVCC avccBox;
      avccBox.setVersion(1);
      avccBox.setProfile(buf.sps[1]);
      avccBox.setCompatibleProfiles(buf.sps[2]);
      avccBox.setLevel(buf.sps[3]);
      avccBox.setSPSNumber(1);
      avccBox.setSPS(buf.sps);
      avccBox.setPPSNumber(1);
      avccBox.setPPS(buf.pps);
      myMeta.tracks[tid].init = std::string(avccBox.payload(), avccBox.payloadSize());
      h264::SPS tmpNal(buf.sps, true);
      h264::SPSMeta tmpMeta = tmpNal.getCharacteristics();
      myMeta.tracks[tid].width = tmpMeta.width;
      myMeta.tracks[tid].height = tmpMeta.height;
      myMeta.tracks[tid].fpks = tmpMeta.fps * 1000;
    }
  }
  
  ///Reads headers from a TS stream, and saves them into metadata
  ///It works by going through the entire TS stream, and every time
  ///It encounters a new PES start, it writes the currently found PES data
  ///for a specific track to metadata. After the entire stream has been read, 
  ///it writes the remaining metadata.
  ///\todo Find errors, perhaps parts can be made more modular
  bool inputTS::readHeader(){
    if (!inFile) {
      return false;
    }
    DTSC::File tmp(config->getString("input") + ".dtsh");
    if (tmp){
      myMeta = tmp.getMeta();
      return true;
    } 
    
    TS::Packet packet;//to analyse and extract data
    fseek(inFile, 0, SEEK_SET);//seek to beginning
    JSON::Value lastPack;

    std::set<int> PATIds;
    std::map<int, int> pidToType;
    std::map<int, pesBuffer> lastBuffer;
    
    //h264::SPSmMta spsdata;//to analyse sps data, and extract resolution etc...
    long long int lastBpos = 0;
    while (packet.FromFile(inFile)){
      //Handle special packets (PAT/PMT)
      if(packet.PID() == 0x00){
        PATIds.clear();
        for (int i = 0; i < ((TS::ProgramAssociationTable&)packet).getProgramCount(); i++){
          PATIds.insert(((TS::ProgramAssociationTable&)packet).getProgramPID(i));
        }
      }
      if(PATIds.count(packet.PID())){
        for (int i = 0; i < ((TS::ProgramMappingTable&)packet).getProgramCount(); i++){
          //Set the correct stream type for each defined PID 
          int pid = (((TS::ProgramMappingTable&)packet).getElementaryPID(i));
          int sType = (((TS::ProgramMappingTable&)packet).getStreamType(i));

          pidToType[pid] = sType;
          //Check if the track exists in metadata 
          if (!myMeta.tracks.count(pid)){
            switch (sType){
              case 0x1B:
                myMeta.tracks[pid].codec = "H264";
                myMeta.tracks[pid].type = "video";
                myMeta.tracks[pid].trackID = pid;
                break;
              case 0x0F:
                myMeta.tracks[pid].codec = "AAC";
                myMeta.tracks[pid].type = "audio";
                myMeta.tracks[pid].trackID = pid;
                break;
              default:
                DEBUG_MSG(DLVL_WARN, "Ignoring unsupported track type %0.2X", pid);
                break;
            }
          }
        }
      }
      if(pidToType.count(packet.PID())){
        //analyzing audio/video
        //we have audio/video payload
        //get trackID of this packet
        int tid = packet.PID();
        if (packet.UnitStart() && lastBuffer.count(tid) && lastBuffer[tid].len){
          parsePESPayload(tid, lastBuffer[tid]);
          lastPack.null();
          lastPack["data"] = lastBuffer[tid].data;
          lastPack["trackid"] = tid;//last trackid
          lastPack["bpos"] = lastBuffer[tid].bpos;
          lastPack["time"] = lastBuffer[tid].time ;
          if (lastBuffer[tid].offset){
            lastPack["offset"] = lastBuffer[tid].offset;
          }
          if (lastBuffer[tid].isKey){
            lastPack["keyframe"] = 1LL;
          }
          myMeta.update(lastPack);//metadata was read in
          lastBuffer.erase(tid);
        }
        if (!lastBuffer.count(tid)){
          lastBuffer[tid] = pesBuffer();
          lastBuffer[tid].bpos = lastBpos;
        }
        lastBuffer[tid].data.append(packet.getPayload(), packet.getPayloadLength());
        if (!lastBuffer[tid].len){
          parsePESHeader(tid, lastBuffer[tid]);
        }
        if (lastBuffer[tid].data.size() == lastBuffer[tid].len) {
          parsePESPayload(tid, lastBuffer[tid]);
          if (myMeta.tracks[tid].codec == "AAC"){
            while(lastBuffer[tid].data.size()){
              lastPack.null();
              lastPack["data"] = lastBuffer[tid].data.substr(0, lastBuffer[tid].len);
              lastPack["trackid"] = tid;//last trackid
              lastPack["bpos"] = lastBuffer[tid].bpos;
              lastPack["time"] = lastBuffer[tid].time + (long long int)((double)((lastBuffer[tid].curSampleCount - 1024) * 1000)/ myMeta.tracks[tid].rate) ;
              myMeta.update(lastPack);//metadata was read in
              lastBuffer[tid].data.erase(0, lastBuffer[tid].len);
              parsePESPayload(tid, lastBuffer[tid]);
            }
          }else{
            lastPack.null();
            lastPack["data"] = lastBuffer[tid].data;
            lastPack["trackid"] = tid;//last trackid
            lastPack["bpos"] = lastBuffer[tid].bpos;
            lastPack["time"] = lastBuffer[tid].time ;
            if (lastBuffer[tid].offset){
              lastPack["offset"] = lastBuffer[tid].offset;
            }
            if (lastBuffer[tid].isKey){
              lastPack["keyframe"] = 1LL;
            }
            myMeta.update(lastPack);//metadata was read in
          }
          lastBuffer.erase(tid);
        }
      }
      lastBpos = ftell(inFile);
    }

    std::ofstream oFile(std::string(config->getString("input") + ".dtsh").c_str());
    oFile << myMeta.toJSON().toNetPacked();
    oFile.close();
    return true;
  }
  
  ///Reads a full PES packet, starting at the current byteposition
  ///Assumes that you want a full PES for the first PID encountered
  ///\todo Update to search for a specific PID
  pesBuffer inputTS::readFullPES(int tid){
    pesBuffer pesBuf;
    pesBuf.tid = tid;
    if (feof(inFile)){
      DEBUG_MSG(DLVL_DEVEL, "Trying to read a PES past the end of the file, returning");
      return pesBuf;
    }
    unsigned int lastPos = ftell(inFile);
    TS::Packet tsBuf;
    tsBuf.FromFile(inFile);
    //Find first PES start on the selected track
    while (tsBuf.PID() != tid || !tsBuf.UnitStart()){
      lastPos = ftell(inFile);
      tsBuf.FromFile(inFile);
      if (feof(inFile)){
        return pesBuf;
      }
    }
    pesBuf.bpos = lastPos;
    pesBuf.data.append(tsBuf.getPayload(), tsBuf.getPayloadLength());
    parsePESHeader(tid, pesBuf);
    bool unbound = false;
    while (pesBuf.data.size() != pesBuf.len){
      //ReadNextPage
      tsBuf.FromFile(inFile);
      if (tsBuf.PID() == tid && tsBuf.UnitStart()){
        unbound = true;
        break;
      }
      if (feof(inFile)){
        DEBUG_MSG(DLVL_DEVEL, "Reached EOF at an unexpected point... what happened?");
        return pesBuf;
      }
      if (tsBuf.PID() == tid){
        pesBuf.data.append(tsBuf.getPayload(), tsBuf.getPayloadLength());
        pesBuf.lastPos = ftell(inFile);
      }
      if (pesBuf.len == 0){
        parsePESHeader(tid, pesBuf);
      }
    }
    pesBuf.lastPos = ftell(inFile);
    if (unbound){
      pesBuf.lastPos -= 188;
    }
    parsePESPayload(tid, pesBuf);
    return pesBuf;
  }

  ///Gets the next packet that is to be sent 
  ///At the moment, the logic of sending the last packet that was finished has been implemented, 
  ///but the seeking and finding data is not yet ready.
  ///\todo Finish the implementation
  void inputTS::getNext(bool smart){
    static JSON::Value thisPack;
    if ( !playbackBuf.size()){
      DEBUG_MSG(DLVL_WARN, "No seek positions set - returning empty packet.");
      lastPack.null();
      return;
    }
    
    //Store current buffer
    pesBuffer thisBuf = *playbackBuf.begin();
    playbackBuf.erase(playbackBuf.begin());

    //Seek follow up
    fseek(inFile, thisBuf.lastPos, SEEK_SET);
    pesBuffer nxtBuf;
    if (myMeta.tracks[thisBuf.tid].codec != "AAC" || playbackBuf.size() < 2){
      nxtBuf = readFullPES(thisBuf.tid);
    }
    if (nxtBuf.len){
      if (myMeta.tracks[nxtBuf.tid].codec == "AAC"){//only in case of aac we have more packets, for now
        while (nxtBuf.len){
          pesBuffer pesBuf;
          pesBuf.tid = nxtBuf.tid;
          pesBuf.time = nxtBuf.time + ((double)((nxtBuf.curSampleCount - 1024) * 1000)/ myMeta.tracks[nxtBuf.tid].rate) ;
          pesBuf.offset = nxtBuf.offset;
          pesBuf.len = nxtBuf.len;
          pesBuf.lastPos = nxtBuf.lastPos;
          pesBuf.isKey = false;
          pesBuf.data = nxtBuf.data.substr(0, nxtBuf.len);
          playbackBuf.insert(pesBuf);

          nxtBuf.data.erase(0, nxtBuf.len);
          parsePESPayload(thisBuf.tid, nxtBuf);
        }
      }else{
        nxtBuf.data = nxtBuf.data.substr(0, nxtBuf.len);
        playbackBuf.insert(nxtBuf);
      }
    }

    thisPack.null();
    thisPack["data"] = thisBuf.data;
    thisPack["trackid"] = thisBuf.tid;
    thisPack["bpos"] = thisBuf.bpos;
    thisPack["time"] = thisBuf.time;
    if (thisBuf.offset){
      thisPack["offset"] = thisBuf.offset;
    }
    if (thisBuf.isKey){
      thisPack["keyframe"] = 1LL;
    }
    std::string tmpStr = thisPack.toNetPacked();
    lastPack.reInit(tmpStr.data(), tmpStr.size());
  }
  
  ///Seeks to a specific time
  void inputTS::seek(int seekTime){
    for (std::set<int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      if (feof(inFile)){
        clearerr(inFile);
        fseek(inFile, 0, SEEK_SET);
      }
      pesBuffer tmpBuf;
      tmpBuf.tid = *it;
      for (unsigned int i = 0; i < myMeta.tracks[*it].keyLen; i++){
        if (myMeta.tracks[*it].keys[i].getTime() > seekTime){
          break;
        }
        if (myMeta.tracks[*it].keys[i].getTime() > tmpBuf.time){
          tmpBuf.time = myMeta.tracks[*it].keys[i].getTime();
          tmpBuf.bpos = myMeta.tracks[*it].keys[i].getBpos();
        }
      }

      bool foundPacket = false;
      unsigned long long lastPos;
      pesBuffer nxtBuf;
      while ( !foundPacket){
        lastPos = ftell(inFile);
        if (feof(inFile)){
          DEBUG_MSG(DLVL_WARN, "Reached EOF during seek to %u in track %d - aborting @ %lld", seekTime, *it, lastPos);
          return;
        }
        fseek(inFile, tmpBuf.bpos, SEEK_SET);
        nxtBuf = readFullPES(*it);
        if (nxtBuf.time >= seekTime){
          foundPacket = true;
        }else{
          tmpBuf.bpos = nxtBuf.lastPos;
        }
      }
      if (myMeta.tracks[nxtBuf.tid].codec == "AAC"){//only in case of aac we have more packets, for now
        while (nxtBuf.len){
          pesBuffer pesBuf;
          pesBuf.tid = nxtBuf.tid;
          pesBuf.time = nxtBuf.time + ((double)((nxtBuf.curSampleCount - 1024) * 1000)/ myMeta.tracks[nxtBuf.tid].rate); 
          pesBuf.offset = nxtBuf.offset;
          pesBuf.len = nxtBuf.len;
          pesBuf.lastPos = nxtBuf.lastPos;
          pesBuf.isKey = false;
          pesBuf.data = nxtBuf.data.substr(0, nxtBuf.len);
          playbackBuf.insert(pesBuf);

          nxtBuf.data.erase(0, nxtBuf.len);
          parsePESPayload(nxtBuf.tid, nxtBuf);
        }
      }else{
        playbackBuf.insert(nxtBuf);
      }
    }
  }
}
