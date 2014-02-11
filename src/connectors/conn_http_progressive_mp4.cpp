///\file conn_http_progressive_mp4.cpp
///\brief Contains the main code for the HTTP Progressive MP4 Connector

#include <iostream>
#include <queue>
#include <sstream>

#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <mist/socket.h>
#include <mist/http_parser.h>
#include <mist/dtsc.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
#include <mist/amf.h>
#include <mist/config.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/defines.h>

///\brief Holds everything unique to HTTP Connectors.
namespace Connector_HTTP {
  
  struct keyPart{
  public:
    bool operator < (const keyPart& rhs) const {
      if (time < rhs.time){
        return true;
      }
      if (time == rhs.time){
        if (trackID < rhs.trackID){
          return true;
        }
      }
      return false;
    }
    long unsigned int trackID;
    long unsigned int size;
    long long unsigned int time;
    long long unsigned int endTime;
    long unsigned int index;
  };
  
  std::string DTSCMeta2MP4Header(DTSC::Meta & metaData, std::set<int> & tracks, long long & size){
    std::stringstream header;
    //ftyp box
    /// \todo fill ftyp with non hardcoded values from file
    MP4::FTYP ftypBox;
    ftypBox.setMajorBrand(0x6D703431);//mp41
    ftypBox.setMinorVersion(0);
    ftypBox.setCompatibleBrands(0x69736f6d,0);
    ftypBox.setCompatibleBrands(0x69736f32,1);
    ftypBox.setCompatibleBrands(0x61766331,2);
    ftypBox.setCompatibleBrands(0x6D703431,3);
    header << std::string(ftypBox.asBox(),ftypBox.boxedSize());
    
    uint64_t mdatSize = 0;
    //moov box
    MP4::MOOV moovBox;{
      //calculating longest duration
      long long int fileDuration = 0;
      long long int firstms = -1;
      long long int lastms = -1;
      for (std::set<int>::iterator it = tracks.begin(); it != tracks.end(); it++) {
        if (lastms == -1 || lastms < metaData.tracks[*it].lastms){
          lastms = metaData.tracks[*it].lastms;
        }
        if (firstms == -1 || firstms > metaData.tracks[*it].firstms){
          firstms = metaData.tracks[*it].firstms;
        }
      }
      fileDuration = lastms - firstms;
      //MP4::MVHD mvhdBox(fileDuration);
      MP4::MVHD mvhdBox;
      mvhdBox.setVersion(0);
      mvhdBox.setCreationTime(0);
      mvhdBox.setModificationTime(0);
      mvhdBox.setTimeScale(1000);
      mvhdBox.setRate(0x10000);
      mvhdBox.setDuration(fileDuration);
      mvhdBox.setTrackID(0);
      mvhdBox.setVolume(256);
      mvhdBox.setMatrix(0x00010000,0);
      mvhdBox.setMatrix(0,1);
      mvhdBox.setMatrix(0,2);
      mvhdBox.setMatrix(0,3);
      mvhdBox.setMatrix(0x00010000,4);
      mvhdBox.setMatrix(0,5);
      mvhdBox.setMatrix(0,6);
      mvhdBox.setMatrix(0,7);
      mvhdBox.setMatrix(0x40000000,8);
      moovBox.setContent(mvhdBox, 0);
    }
    {//start arbitrary track addition for headeri
      int boxOffset = 1;
      for (std::set<int>::iterator it = tracks.begin(); it != tracks.end(); it++) {
        int timescale = 0;
        MP4::TRAK trakBox;
        {
          {
            MP4::TKHD tkhdBox;
            tkhdBox.setVersion(0);
            tkhdBox.setFlags(15);
            tkhdBox.setTrackID(*it);
            tkhdBox.setDuration(metaData.tracks[*it].lastms - metaData.tracks[*it].firstms);
            
            if (metaData.tracks[*it].type == "video"){
              tkhdBox.setWidth(metaData.tracks[*it].width << 16);
              tkhdBox.setHeight(metaData.tracks[*it].height << 16);
              tkhdBox.setVolume(0);
            }else{
              tkhdBox.setVolume(256);
              tkhdBox.setAlternateGroup(1);
            }
            tkhdBox.setMatrix(0x00010000,0);
            tkhdBox.setMatrix(0,1);
            tkhdBox.setMatrix(0,2);
            tkhdBox.setMatrix(0,3);
            tkhdBox.setMatrix(0x00010000,4);
            tkhdBox.setMatrix(0,5);
            tkhdBox.setMatrix(0,6);
            tkhdBox.setMatrix(0,7);
            tkhdBox.setMatrix(0x40000000,8);
            trakBox.setContent(tkhdBox, 0);
          }{
            MP4::MDIA mdiaBox;
            {
              MP4::MDHD mdhdBox(0);/// \todo fix constructor mdhd in lib
              mdhdBox.setCreationTime(0);
              mdhdBox.setModificationTime(0);
              //Calculating media time based on sampledelta. Probably cheating, but it works...
              timescale = ((double)(42 * metaData.tracks[*it].parts.size() ) / (metaData.tracks[*it].lastms - metaData.tracks[*it].firstms)) *  1000;
              mdhdBox.setTimeScale(timescale);
              mdhdBox.setDuration((metaData.tracks[*it].lastms - metaData.tracks[*it].firstms) * ((double)timescale / 1000));
              mdiaBox.setContent(mdhdBox, 0);
            }//MDHD box
            {
              MP4::HDLR hdlrBox;/// \todo fix constructor hdlr in lib
              if (metaData.tracks[*it].type == "video"){
                hdlrBox.setHandlerType(0x76696465);//vide
              }else if (metaData.tracks[*it].type == "audio"){
                hdlrBox.setHandlerType(0x736F756E);//soun
              }
              hdlrBox.setName(metaData.tracks[*it].getIdentifier());
              mdiaBox.setContent(hdlrBox, 1);
            }//hdlr box
            {
              MP4::MINF minfBox;
              unsigned int minf_offset = 0;
              if (metaData.tracks[*it].type== "video"){
                MP4::VMHD vmhdBox;
                vmhdBox.setFlags(1);
                minfBox.setContent(vmhdBox,minf_offset++);
              }else if (metaData.tracks[*it].type == "audio"){
                MP4::SMHD smhdBox;
                minfBox.setContent(smhdBox,minf_offset++);
              }//type box
              {
                MP4::DINF dinfBox;
                MP4::DREF drefBox;/// \todo fix constructor dref in lib
                drefBox.setVersion(0);
                MP4::URL urlBox;
                urlBox.setFlags(1);
                drefBox.setDataEntry(urlBox,0);
                dinfBox.setContent(drefBox,0);
                minfBox.setContent(dinfBox,minf_offset++);
              }//dinf box
              {
                MP4::STBL stblBox;
                unsigned int offset = 0;
                {
                  MP4::STSD stsdBox;
                  stsdBox.setVersion(0);
                  if (metaData.tracks[*it].type == "video"){//boxname = codec
                    MP4::VisualSampleEntry vse;
                    if (metaData.tracks[*it].codec == "H264"){
                      vse.setCodec("avc1");
                    }
                    vse.setDataReferenceIndex(1);
                    vse.setWidth(metaData.tracks[*it].width);
                    vse.setHeight(metaData.tracks[*it].height);
                    MP4::AVCC avccBox;
                    avccBox.setPayload(metaData.tracks[*it].init);
                    vse.setCLAP(avccBox);
                    stsdBox.setEntry(vse,0);
                  }else if(metaData.tracks[*it].type == "audio"){//boxname = codec
                    MP4::AudioSampleEntry ase;
                    if (metaData.tracks[*it].codec == "AAC"){
                      ase.setCodec("mp4a");
                      ase.setDataReferenceIndex(1);
                    }
                    ase.setSampleRate(metaData.tracks[*it].rate);
                    ase.setChannelCount(metaData.tracks[*it].channels);
                    ase.setSampleSize(metaData.tracks[*it].size);
                    //MP4::ESDS esdsBox(metaData.tracks[*it].init, metaData.tracks[*it].bps);
                    MP4::ESDS esdsBox;
                    
                    //outputting these values first, so malloc isn't called as often.
                    esdsBox.setESHeaderStartCodes(metaData.tracks[*it].init);
                    esdsBox.setSLValue(2);
                    
                    esdsBox.setESDescriptorTypeLength(32+metaData.tracks[*it].init.size());
                    esdsBox.setESID(2);
                    esdsBox.setStreamPriority(0);
                    esdsBox.setDecoderConfigDescriptorTypeLength(18 + metaData.tracks[*it].init.size());
                    esdsBox.setByteObjectTypeID(0x40);
                    esdsBox.setStreamType(5);
                    esdsBox.setReservedFlag(1);
                    esdsBox.setBufferSize(1250000);
                    esdsBox.setMaximumBitRate(10000000);
                    esdsBox.setAverageBitRate(metaData.tracks[*it].bps * 8);
                    esdsBox.setConfigDescriptorTypeLength(5);
                    esdsBox.setSLConfigDescriptorTypeTag(0x6);
                    esdsBox.setSLConfigExtendedDescriptorTypeTag(0x808080);
                    esdsBox.setSLDescriptorTypeLength(1);
                    ase.setCodecBox(esdsBox);
                    stsdBox.setEntry(ase,0);
                  }
                  stblBox.setContent(stsdBox,offset++);
                }//stsd box
                /// \todo update following stts lines
                {
                  MP4::STTS sttsBox;//current version probably causes problems
                  sttsBox.setVersion(0);
                  MP4::STTSEntry newEntry;
                  newEntry.sampleCount = metaData.tracks[*it].parts.size();
                  //42, Used as magic number for timescale calculation
                  newEntry.sampleDelta = 42;
                  sttsBox.setSTTSEntry(newEntry, 0);
                  stblBox.setContent(sttsBox,offset++);
                }//stts box
                if (metaData.tracks[*it].type == "video"){
                  //STSS Box here
                  MP4::STSS stssBox;
                  stssBox.setVersion(0);
                  int tmpCount = 0;
                  int tmpItCount = 0;
                  for ( std::deque< DTSC::Key>::iterator tmpIt = metaData.tracks[*it].keys.begin(); tmpIt != metaData.tracks[*it].keys.end(); tmpIt ++) {
                    stssBox.setSampleNumber(tmpCount,tmpItCount);
                    tmpCount += tmpIt->getParts();
                    tmpItCount ++;
                  }
                  stblBox.setContent(stssBox,offset++);
                }//stss box
                {
                  MP4::STSC stscBox;
                  stscBox.setVersion(0);
                  MP4::STSCEntry stscEntry;
                  stscEntry.firstChunk = 1;
                  stscEntry.samplesPerChunk = 1;
                  stscEntry.sampleDescriptionIndex = 1;
                  stscBox.setSTSCEntry(stscEntry, 0);
                  stblBox.setContent(stscBox,offset++);
                }//stsc box
                {
                  uint32_t total = 0;
                  MP4::STSZ stszBox;
                  stszBox.setVersion(0);
                  total = 0;
                  for (std::deque< DTSC::Part>::iterator partIt = metaData.tracks[*it].parts.begin(); partIt != metaData.tracks[*it].parts.end(); partIt ++) {
                    stszBox.setEntrySize(partIt->getSize(), total);//in bytes in file
                    size += partIt->getSize();
                    total++;
                  }
                  stblBox.setContent(stszBox,offset++);
                }//stsz box
                //add STCO boxes here
                {
                  MP4::STCO stcoBox;
                  stcoBox.setVersion(1);
                  //Inserting empty values on purpose here, will be fixed later.
                  if (metaData.tracks[*it].parts.size() != 0){
                    stcoBox.setChunkOffset(0, metaData.tracks[*it].parts.size() - 1);//this inserts all empty entries at once
                  }
                  stblBox.setContent(stcoBox,offset++);
                }//stco box
                minfBox.setContent(stblBox,minf_offset++);
              }//stbl box
              mdiaBox.setContent(minfBox, 2);
            }//minf box
            trakBox.setContent(mdiaBox, 1);
          }
        }//trak Box
        moovBox.setContent(trakBox, boxOffset);
        boxOffset++;
      }
    }//end arbitrary track addition
    //initial offset length ftyp, length moov + 8
    unsigned long long int byteOffset = ftypBox.boxedSize() + moovBox.boxedSize() + 8;
    //update all STCO from the following map;
    std::map <int, MP4::STCO> checkStcoBoxes;
    //for all tracks
    for (unsigned int i = 1; i < moovBox.getContentCount(); i++){
      //10 lines to get the STCO box.
      MP4::TRAK checkTrakBox;
      MP4::MDIA checkMdiaBox;
      MP4::TKHD checkTkhdBox;
      MP4::MINF checkMinfBox;
      MP4::STBL checkStblBox;
      //MP4::STCO checkStcoBox;
      checkTrakBox = ((MP4::TRAK&)moovBox.getContent(i));
      for (unsigned int j = 0; j < checkTrakBox.getContentCount(); j++){
        if (checkTrakBox.getContent(j).isType("mdia")){
          checkMdiaBox = ((MP4::MDIA&)checkTrakBox.getContent(j));
          break;
        }
        if (checkTrakBox.getContent(j).isType("tkhd")){
          checkTkhdBox = ((MP4::TKHD&)checkTrakBox.getContent(j));
        }
      }
      for (unsigned int j = 0; j < checkMdiaBox.getContentCount(); j++){
        if (checkMdiaBox.getContent(j).isType("minf")){
          checkMinfBox = ((MP4::MINF&)checkMdiaBox.getContent(j));
          break;
        }
      }
      for (unsigned int j = 0; j < checkMinfBox.getContentCount(); j++){
        if (checkMinfBox.getContent(j).isType("stbl")){
          checkStblBox = ((MP4::STBL&)checkMinfBox.getContent(j));
          break;
        }
      }
      for (unsigned int j = 0; j < checkStblBox.getContentCount(); j++){
        if (checkStblBox.getContent(j).isType("stco")){
          checkStcoBoxes.insert( std::pair<int, MP4::STCO>(checkTkhdBox.getTrackID(), ((MP4::STCO&)checkStblBox.getContent(j)) ));
          break;
        }
      }
    }
    //inserting right values in the STCO box header
    //total = 0;
    long long unsigned int totalByteOffset = 0;
    //Current values are actual byte offset without header-sized offset
    std::set <keyPart> sortSet;//filling sortset for interleaving parts
    for (std::set<int>::iterator subIt = tracks.begin(); subIt != tracks.end(); subIt++) {
      keyPart temp;
      temp.trackID = *subIt;
      temp.time = metaData.tracks[*subIt].firstms;//timeplace of frame
      temp.endTime = metaData.tracks[*subIt].firstms + metaData.tracks[*subIt].parts[0].getDuration();
      temp.size = metaData.tracks[*subIt].parts[0].getSize();//bytesize of frame (alle parts all together)
      temp.index = 0;
      sortSet.insert(temp);
    }
    while (!sortSet.empty()){
      //setting the right STCO size in the STCO box
      checkStcoBoxes[sortSet.begin()->trackID].setChunkOffset(totalByteOffset + byteOffset, sortSet.begin()->index);
      totalByteOffset += sortSet.begin()->size;
      //add keyPart to sortSet
      keyPart temp;
      temp.index = sortSet.begin()->index + 1;
      temp.trackID = sortSet.begin()->trackID;
      if(temp.index < metaData.tracks[temp.trackID].parts.size() ){//only insert when there are parts left
        temp.time = sortSet.begin()->endTime;//timeplace of frame
        temp.endTime = sortSet.begin()->endTime + metaData.tracks[temp.trackID].parts[temp.index].getDuration();
        temp.size = metaData.tracks[temp.trackID].parts[temp.index].getSize();//bytesize of frame 
        sortSet.insert(temp);
      }
      //remove highest keyPart
      sortSet.erase(sortSet.begin());
    }
    //calculating the offset where the STCO box will be in the main MOOV box
    //needed for probable optimise
    mdatSize = totalByteOffset;
    
    header << std::string(moovBox.asBox(),moovBox.boxedSize());
    
    header << (char)((mdatSize>>24) & 0x000000FF) << (char)((mdatSize>>16) & 0x000000FF) << (char)((mdatSize>>8) & 0x000000FF) << (char)(mdatSize & 0x000000FF) << "mdat";
    //end of header
    
    size += header.str().size();
    return header.str();
  }
  
  /// Calculate a seekPoint, based on byteStart, metadata, tracks and headerSize.
  /// The seekPoint will be set to the timestamp of the first packet to send.
  void findSeekPoint(long long byteStart, long long & seekPoint, DTSC::Meta & metadata, std::set<int> & tracks, unsigned int headerSize){
    seekPoint = 0;
    //if we're starting in the header, seekPoint is always zero.
    if (byteStart <= headerSize){return;}
    //okay, we're past the header. Substract the headersize from the starting postion.
    byteStart -= headerSize;
    //initialize a list of sorted parts that this file contains
    std::set <keyPart> sortSet;
    for (std::set<int>::iterator subIt = tracks.begin(); subIt != tracks.end(); subIt++) {
      keyPart temp;
      temp.trackID = *subIt;
      temp.time = metadata.tracks[*subIt].firstms;//timeplace of frame
      temp.endTime = metadata.tracks[*subIt].firstms + metadata.tracks[*subIt].parts[0].getDuration();
      temp.size = metadata.tracks[*subIt].parts[0].getSize();//bytesize of frame (alle parts all together)
      temp.index = 0;
      sortSet.insert(temp);
    }
    //forward through the file by headers, until we reach the point where we need to be
    while (!sortSet.empty()){
      //substract the size of this fragment from byteStart
      byteStart -= sortSet.begin()->size;
      //if that put us past the point where we wanted to be, return right now
      if (byteStart < 0){return;}
      //otherwise, set seekPoint to where we are now
      seekPoint = sortSet.begin()->time;
      //then find the next part
      keyPart temp;
      temp.index = sortSet.begin()->index + 1;
      temp.trackID = sortSet.begin()->trackID;
      if(temp.index < metadata.tracks[temp.trackID].parts.size() ){//only insert when there are parts left
        temp.time = sortSet.begin()->endTime;//timeplace of frame
        temp.endTime = sortSet.begin()->endTime + metadata.tracks[temp.trackID].parts[temp.index].getDuration();
        temp.size = metadata.tracks[temp.trackID].parts[temp.index].getSize();//bytesize of frame 
        sortSet.insert(temp);
      }
      //remove highest keyPart
      sortSet.erase(sortSet.begin());
    }
    //If we're here, we're in the last fragment.
    //That's technically legal, of course.
  }
  
  /// Parses a "Range: " header, setting byteStart, byteEnd and seekPoint using data from metadata and tracks to do
  /// the calculations.
  /// On error, byteEnd is set to zero.
  void parseRange(std::string header, long long & byteStart, long long & byteEnd, long long & seekPoint, DTSC::Meta & metadata, std::set<int> & tracks, unsigned int headerSize){
    if (header.size() < 6 || header.substr(0, 6) != "bytes="){
      byteEnd = 0;
      DEBUG_MSG(DLVL_WARN, "Invalid range header: %s", header.c_str());
      return;
    }
    header.erase(0, 6);
    if (header.size() && header[0] == '-'){
      //negative range = count from end
      byteStart = 0;
      for (unsigned int i = 1; i < header.size(); ++i){
        if (header[i] >= '0' && header[i] <= '9'){
          byteStart *= 10;
          byteStart += header[i] - '0';
          continue;
        }
        break;
      }
      if (byteStart > byteEnd){
        //entire file if starting before byte zero
        byteStart = 0;
        DEBUG_MSG(DLVL_DEVEL, "Full negative range: %lli-%lli", byteStart, byteEnd);
        findSeekPoint(byteStart, seekPoint, metadata, tracks, headerSize);
        return;
      }else{
        //start byteStart bytes before byteEnd
        byteStart = byteEnd - byteStart;
        DEBUG_MSG(DLVL_DEVEL, "Partial negative range: %lli-%lli", byteStart, byteEnd);
        findSeekPoint(byteStart, seekPoint, metadata, tracks, headerSize);
        return;
      }
    }else{
      long long size = byteEnd;
      byteEnd = 0;
      byteStart = 0;
      unsigned int i = 0;
      for ( ; i < header.size(); ++i){
        if (header[i] >= '0' && header[i] <= '9'){
          byteStart *= 10;
          byteStart += header[i] - '0';
          continue;
        }
        break;
      }
      if (header[i] != '-'){
        DEBUG_MSG(DLVL_WARN, "Invalid range header: %s", header.c_str());
        byteEnd = 0;
        return;
      }
      ++i;
      if (i < header.size()){
        for ( ; i < header.size(); ++i){
          if (header[i] >= '0' && header[i] <= '9'){
            byteEnd *= 10;
            byteEnd += header[i] - '0';
            continue;
          }
          break;
        }
        if (byteEnd > size-1){byteEnd = size;}
      }else{
        byteEnd = size;
      }
      DEBUG_MSG(DLVL_DEVEL, "Range request: %lli-%lli (%s)", byteStart, byteEnd, header.c_str());
      findSeekPoint(byteStart, seekPoint, metadata, tracks, headerSize);
      return;
    }
  }//parseRange
  
  ///\brief Main function for the HTTP Progressive Connector
  ///\param conn A socket describing the connection the client.
  ///\return The exit code of the connector.
  int progressiveConnector(Socket::Connection & conn){
    DTSC::Stream Strm; //Incoming stream buffer.
    HTTP::Parser HTTP_R, HTTP_S;//HTTP Receiver en HTTP Sender.
    long long byteStart = 0;
    long long leftOver = 0;
    long long currPos = 0;
    bool inited = false;//Whether the stream is initialized
    Socket::Connection ss( -1);//The Stream Socket, used to connect to the desired stream.
    std::string streamname;//Will contain the name of the stream.
    std::set <keyPart> sortSet;//filling sortset for interleaving parts

    unsigned int lastStats = 0;//Indicates the last time that we have sent stats to the server socket.
    
    while (conn.connected()){
      //Only attempt to parse input when not yet init'ed.
      if ( !inited){
        if (conn.Received().size() || conn.spool()){
          if (HTTP_R.Read(conn)){
            DEBUG_MSG(DLVL_DEVEL, "Received request: %s", HTTP_R.getUrl().c_str());
            conn.setHost(HTTP_R.GetHeader("X-Origin"));
            streamname = HTTP_R.GetHeader("X-Stream");
            if (!ss){
              ss = Util::Stream::getStream(streamname);
              if (!ss){
                DEBUG_MSG(DLVL_FAIL, "Could not connect to stream %s!", streamname.c_str());
                ss.close();
                HTTP_S.Clean();
                HTTP_S.SetBody("No such stream is available on the system. Please try again.\n");
                HTTP_S.SendResponse("404", "Not found", conn);
                continue;
              }
              Strm.waitForMeta(ss);
            }
            int videoID = -1;
            int audioID = -1;
            if (HTTP_R.GetVar("audio") != ""){
              audioID = JSON::Value(HTTP_R.GetVar("audio")).asInt();
            }
            if (HTTP_R.GetVar("video") != ""){
              videoID = JSON::Value(HTTP_R.GetVar("video")).asInt();
            }
            for (std::map<int,DTSC::Track>::iterator it = Strm.metadata.tracks.begin(); it != Strm.metadata.tracks.end(); it++){
              if (videoID == -1 && it->second.type == "video" && it->second.codec == "H264"){
                videoID = it->first;
              }
              if (audioID == -1 && it->second.type == "audio" && it->second.codec == "AAC"){
                audioID = it->first;
              }
            }
            
            std::set<int> tracks;
            if (videoID > 0){tracks.insert(videoID);}
            if (audioID > 0){tracks.insert(audioID);}
            
            HTTP_S.Clean(); //make sure no parts of old requests are left in any buffers
            HTTP_S.SetHeader("Content-Type", "video/MP4"); //Send the correct content-type for MP4 files
            HTTP_S.SetHeader("Accept-Ranges", "bytes, parsec");
            long long size = 0;
            std::string headerData = DTSCMeta2MP4Header(Strm.metadata, tracks, size);
            byteStart = 0;
            long long byteEnd = size-1;
            long long seekPoint = 0;
            if (HTTP_R.GetHeader("Range") != ""){
              parseRange(HTTP_R.GetHeader("Range"), byteStart, byteEnd, seekPoint, Strm.metadata, tracks, headerData.size());
              if (!byteEnd){
                if (HTTP_R.GetHeader("Range")[0] == 'p'){
                  HTTP_S.SetBody("Starsystem not in communications range");
                  HTTP_S.SendResponse("416", "Starsystem not in communications range", conn);
                  HTTP_R.Clean(); //clean for any possible next requests
                  continue;
                }else{
                  HTTP_S.SetBody("Requested Range Not Satisfiable");
                  HTTP_S.SendResponse("416", "Requested Range Not Satisfiable", conn);
                  HTTP_R.Clean(); //clean for any possible next requests
                  continue;
                }
              }else{
                std::stringstream rangeReply;
                rangeReply << "bytes " << byteStart << "-" << byteEnd << "/" << size;
                HTTP_S.SetHeader("Content-Length", byteEnd - byteStart + 1);
                HTTP_S.SetHeader("Content-Range", rangeReply.str());
                /// \todo Switch to chunked?
                HTTP_S.SendResponse("206", "Partial content", conn);
                //HTTP_S.StartResponse("206", "Partial content", HTTP_R, conn);
              }
            }else{
              HTTP_S.SetHeader("Content-Length", byteEnd - byteStart + 1);
              /// \todo Switch to chunked?
              HTTP_S.SendResponse("200", "OK", conn);
              //HTTP_S.StartResponse(HTTP_R, conn);
            }
            leftOver = byteEnd - byteStart + 1;//add one byte, because range "0-0" = 1 byte of data
            currPos = 0;
            if (byteStart < (long long)headerData.size()){
              /// \todo Switch to chunked?
              //HTTP_S.Chunkify(headerData.data()+byteStart, std::min((long long)headerData.size(), byteEnd) - byteStart, conn);//send MP4 header
              conn.SendNow(headerData.data()+byteStart, std::min((long long)headerData.size(), byteEnd) - byteStart);//send MP4 header
              leftOver -= std::min((long long)headerData.size(), byteEnd) - byteStart;
            }
            currPos = headerData.size();//we're now guaranteed to be past the header point, no matter what
            HTTP_R.Clean(); //clean for any possible next requests
            {//using scope to have cmd not declared after action
              std::stringstream cmd;
              cmd << "t";
              for (std::set<int>::iterator it = tracks.begin(); it != tracks.end(); it++) {
                cmd << " " << *it;
              }
              cmd << "\ns " << seekPoint << "\np\n";
              ss.SendNow(cmd.str());
            }
            sortSet.clear();
            for (std::set<int>::iterator subIt = tracks.begin(); subIt != tracks.end(); subIt++) {
              keyPart temp;
              temp.trackID = *subIt;
              temp.time = Strm.metadata.tracks[*subIt].firstms;//timeplace of frame
              temp.endTime = Strm.metadata.tracks[*subIt].firstms + Strm.metadata.tracks[*subIt].parts[0].getDuration();
              temp.size = Strm.metadata.tracks[*subIt].parts[0].getSize();//bytesize of frame (alle parts all together)
              temp.index = 0;
              sortSet.insert(temp);
            }
            inited = true;
          }
        }
      }else{
        unsigned int now = Util::epoch();
        if (now != lastStats){
          lastStats = now;
          ss.SendNow(conn.getStats("HTTP_Progressive_MP4").c_str());
        }
        if (ss.spool()){
          while (Strm.parsePacket(ss.Received())){
            if (Strm.lastType() == DTSC::PAUSEMARK){
              conn.close();
            }else if(Strm.lastType() == DTSC::AUDIO || Strm.lastType() == DTSC::VIDEO){
              //keep track of where we are - fast-forward until where we are now
              while (!sortSet.empty() && ((long long)sortSet.begin()->trackID != Strm.getPacket()["trackid"].asInt() || (long long)sortSet.begin()->time != Strm.getPacket()["time"].asInt())){
                keyPart temp;
                temp.index = sortSet.begin()->index + 1;
                temp.trackID = sortSet.begin()->trackID;
                if(temp.index < Strm.metadata.tracks[temp.trackID].parts.size() ){//only insert when there are parts left
                  temp.time = sortSet.begin()->endTime;//timeplace of frame
                  temp.endTime = sortSet.begin()->endTime + Strm.metadata.tracks[temp.trackID].parts[temp.index].getDuration();
                  temp.size = Strm.metadata.tracks[temp.trackID].parts[temp.index].getSize();//bytesize of frame 
                  sortSet.insert(temp);
                }
                currPos += sortSet.begin()->size;
                //remove highest keyPart
                sortSet.erase(sortSet.begin());
              }
              if (currPos >= byteStart){
                sortSet.clear();//we don't need you anymore!
                if (leftOver < (long long)Strm.lastData().size()){
                  conn.SendNow(Strm.lastData().data(), leftOver);
                }else{
                  conn.SendNow(Strm.lastData());
                }
                //HTTP_S.Chunkify(Strm.lastData().data(), Strm.lastData().size(), conn);
                leftOver -= Strm.lastData().size();
              }else{
                if (currPos + (long long)Strm.lastData().size() > byteStart){
                  conn.SendNow(Strm.lastData().data()+(byteStart-currPos), Strm.lastData().size()-(byteStart-currPos));
                  leftOver -= Strm.lastData().size()-(byteStart-currPos);
                  currPos = byteStart;
                  sortSet.clear();//we don't need you anymore!
                }
              }
              if (leftOver < 1){
                ss.SendNow("q\n");//stop playback
                inited = false;
              }
            }
            if (Strm.lastType() == DTSC::INVALID){
              DEBUG_MSG(DLVL_FAIL, "Invalid packet received - closing connection");
              conn.close();
            }
          }
        }else{
          Util::sleep(10);
        }
        if ( !ss.connected()){
          break;
        }
      }
    }
    conn.close();
    ss.SendNow(conn.getStats("HTTP_Progressive_MP4").c_str());
    ss.close();
    return 0;
  } //Progressive_Connector main function

} //Connector_HTTP namespace

///\brief The standard process-spawning main function.
int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  JSON::Value capa;
  capa["desc"] = "Enables HTTP protocol progressive streaming.";
  capa["deps"] = "HTTP";
  capa["url_rel"] = "/$.mp4";
  capa["url_match"] = "/$.mp4";
  capa["codecs"][0u][0u].append("H264");
  capa["codecs"][0u][1u].append("AAC");
  capa["methods"][0u]["handler"] = "http";
  capa["methods"][0u]["type"] = "html5/video/mp4";
  capa["methods"][0u]["priority"] = 8ll;
  capa["methods"][0u]["nolive"] = 1;
  capa["socket"] = "http_progressive_mp4";
  conf.addBasicConnectorOptions(capa);
  conf.parseArgs(argc, argv);
  
  if (conf.getBool("json")){
    std::cout << capa.toString() << std::endl;
    return -1;
  }
  
  return conf.serveForkedSocket(Connector_HTTP::progressiveConnector);
} //main
