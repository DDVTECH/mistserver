#include "oggconv.h"
#include <stdlib.h>
#include <mist/bitstream.h>


namespace OGG{
  void converter::readDTSCHeader(DTSC::Meta & meta){
    //pages.clear();
    parsedPages = "";
    Page curOggPage;
    srand (Util::getMS());//randomising with milliseconds from boot
    std::vector<unsigned int> curSegTable;
    //trackInf.clear();
    /// \todo This is utter rubbish right now.
    /// \todo We shouldn't assume all possible tracks are selected.
    /// \todo We shouldn't be buffering, but sending.
    /// \todo Especially not in a std::string. (Why, god, why?!)
    //Creating headers
    for ( std::map<int,DTSC::Track>::iterator it = meta.tracks.begin(); it != meta.tracks.end(); it ++) {
      trackInf[it->second.trackID].codec = it->second.codec;
      trackInf[it->second.trackID].OGGSerial = rand() % 0xFFFFFFFE +1; //initialising on a random not 0 number
      trackInf[it->second.trackID].seqNum = 0;
      if (it->second.codec == "theora"){
        curOggPage.clear();
        curOggPage.setVersion();
        curOggPage.setHeaderType(2);//headertype 2 = Begin of Stream
        curOggPage.setGranulePosition(0);
        curOggPage.setBitstreamSerialNumber(trackInf[it->second.trackID].OGGSerial);
        curOggPage.setPageSequenceNumber(trackInf[it->second.trackID].seqNum++);
        curSegTable.clear();
        curSegTable.push_back(it->second.idHeader.size());
        curOggPage.setSegmentTable(curSegTable);
        curOggPage.setPayload((char*)it->second.idHeader.c_str(), it->second.idHeader.size());
        curOggPage.setCRCChecksum(curOggPage.calcChecksum());
        parsedPages += std::string(curOggPage.getPage(), curOggPage.getPageSize());
        trackInf[it->second.trackID].lastKeyFrame = 1;
        trackInf[it->second.trackID].sinceKeyFrame = 0;
        theora::header tempHead;
        std::string tempString = it->second.idHeader;
        tempHead.read((char*)tempString.c_str(),42);
        trackInf[it->second.trackID].significantValue = tempHead.getKFGShift();
        curOggPage.clear();
        curOggPage.setVersion();
        curOggPage.setHeaderType(0);//headertype 0 = normal
        curOggPage.setGranulePosition(0);
        curOggPage.setBitstreamSerialNumber(trackInf[it->second.trackID].OGGSerial);
        curOggPage.setPageSequenceNumber(trackInf[it->second.trackID].seqNum++);
        curSegTable.clear();
        curSegTable.push_back(it->second.commentHeader.size());
        curSegTable.push_back(it->second.init.size());
        curOggPage.setSegmentTable(curSegTable);
        std::string fullHeader = it->second.commentHeader + it->second.init;
        curOggPage.setPayload((char*)fullHeader.c_str(),fullHeader.size());
        curOggPage.setCRCChecksum(curOggPage.calcChecksum());
        parsedPages += std::string(curOggPage.getPage(), curOggPage.getPageSize());
      }else if (it->second.codec == "vorbis"){
        curOggPage.clear();
        curOggPage.setVersion();
        curOggPage.setHeaderType(2);//headertype 2 = Begin of Stream
        curOggPage.setGranulePosition(0);
        curOggPage.setBitstreamSerialNumber(trackInf[it->second.trackID].OGGSerial);
        curOggPage.setPageSequenceNumber(trackInf[it->second.trackID].seqNum++);
        curSegTable.clear();
        curSegTable.push_back(it->second.idHeader.size());
        curOggPage.setSegmentTable(curSegTable);
        curOggPage.setPayload((char*)it->second.idHeader.c_str(), it->second.idHeader.size());
        curOggPage.setCRCChecksum(curOggPage.calcChecksum());
        parsedPages += std::string(curOggPage.getPage(), curOggPage.getPageSize());
        trackInf[it->second.trackID].lastKeyFrame = 0;
        trackInf[it->second.trackID].sinceKeyFrame = 0;
        trackInf[it->second.trackID].prevBlockFlag = -1;
        vorbis::header tempHead;
        std::string tempString = it->second.idHeader;
        tempHead.read((char*)tempString.c_str(),tempString.size());
        trackInf[it->second.trackID].significantValue = tempHead.getAudioSampleRate() / tempHead.getAudioChannels();
        if (tempHead.getBlockSize0() <= tempHead.getBlockSize1()){
          trackInf[it->second.trackID].blockSize[0] = tempHead.getBlockSize0();
          trackInf[it->second.trackID].blockSize[1] = tempHead.getBlockSize1();
        }else{
          trackInf[it->second.trackID].blockSize[0] = tempHead.getBlockSize1();
          trackInf[it->second.trackID].blockSize[1] = tempHead.getBlockSize0();
        }
        char audioChannels = tempHead.getAudioChannels();
        //getting modes
        tempString = it->second.init;
        tempHead.read((char*)tempString.c_str(),tempString.size());
        trackInf[it->second.trackID].vorbisModes = tempHead.readModeDeque(audioChannels);
        trackInf[it->second.trackID].hadFirst = false;
        curOggPage.clear();
        curOggPage.setVersion();
        curOggPage.setHeaderType(0);//headertype 0 = normal
        curOggPage.setGranulePosition(0);
        curOggPage.setBitstreamSerialNumber(trackInf[it->second.trackID].OGGSerial);
        curOggPage.setPageSequenceNumber(trackInf[it->second.trackID].seqNum++);
        curSegTable.clear();
        curSegTable.push_back(it->second.commentHeader.size());
        curSegTable.push_back(it->second.init.size());
        curOggPage.setSegmentTable(curSegTable);
        std::string fullHeader = it->second.commentHeader + it->second.init;
        curOggPage.setPayload((char*)fullHeader.c_str(),fullHeader.size());
        curOggPage.setCRCChecksum(curOggPage.calcChecksum());
        parsedPages += std::string(curOggPage.getPage(), curOggPage.getPageSize());
      }else if (it->second.codec == "opus"){
        //OpusHead page
        curOggPage.clear();
        curOggPage.setVersion();
        curOggPage.setHeaderType(2);//headertype 2 = Begin of Stream
        curOggPage.setGranulePosition(0);
        curOggPage.setBitstreamSerialNumber(trackInf[it->second.trackID].OGGSerial);
        curOggPage.setPageSequenceNumber(trackInf[it->second.trackID].seqNum++);
        curSegTable.clear();
        curSegTable.push_back(19);
        curOggPage.setSegmentTable(curSegTable);
        //version = 1, channels = 2, preskip=0x138, origRate=48k, gain=0, channelmap=0
        //we can safely hard-code these as everything is already overridden elsewhere anyway
        // (except preskip - but this seems to be 0x138 for all files, and doesn't hurt much if it's wrong anyway)
        curOggPage.setPayload((char*)"OpusHead\001\002\070\001\200\273\000\000\000\000\000", 19);
        curOggPage.setCRCChecksum(curOggPage.calcChecksum());
        parsedPages += std::string(curOggPage.getPage(), curOggPage.getPageSize());
        //end of OpusHead, now moving on to OpusTags
        curOggPage.clear();
        curOggPage.setVersion();
        curOggPage.setHeaderType(2);//headertype 2 = Begin of Stream
        curOggPage.setGranulePosition(0);
        curOggPage.setBitstreamSerialNumber(trackInf[it->second.trackID].OGGSerial);
        curOggPage.setPageSequenceNumber(trackInf[it->second.trackID].seqNum++);
        curSegTable.clear();
        curSegTable.push_back(26);
        curOggPage.setSegmentTable(curSegTable);
        //we send an encoder value of "MistServer" and no further tags
        curOggPage.setPayload((char*)"OpusTags\012\000\000\000MistServer\000\000\000\000", 26);
        curOggPage.setCRCChecksum(curOggPage.calcChecksum());
        parsedPages += std::string(curOggPage.getPage(), curOggPage.getPageSize());
      }
    }
  }
  
  void converter::readDTSCVector(JSON::Value & DTSCPart, std::string & pageBuffer){
    Page retVal;
    int typeFlag = 0;//flag to remember if the page has a continued segment
    long long int DTSCID = DTSCPart["trackid"].asInt();
    std::vector<unsigned int> curSegTable;
    std::string dataBuffer;
    long long unsigned int lastGran = 0;

    //for (unsigned int i = 0; i < DTSCVec.size(); i++){
    OGG::Page tempPage;
    tempPage.setSegmentTable(curSegTable);
    if (DTSCPart["data"].asString().size() >= (255-tempPage.getPageSegments())*255u){//if segment is too big
      //Put page in Buffer and start next page
      if (!curSegTable.empty()){
        //output page
        retVal.clear();
        retVal.setVersion();
        retVal.setHeaderType(typeFlag);//headertype 0 = normal
        retVal.setGranulePosition(lastGran);
        retVal.setBitstreamSerialNumber(trackInf[DTSCID].OGGSerial);
        retVal.setPageSequenceNumber(trackInf[DTSCID].seqNum);
        retVal.setSegmentTable(curSegTable);
        retVal.setPayload((char*)dataBuffer.c_str(), dataBuffer.size());
        retVal.setCRCChecksum(retVal.calcChecksum());
        trackInf[DTSCID].seqNum++;
        pageBuffer += std::string((char*)retVal.getPage(), retVal.getPageSize());
        
        curSegTable.clear();
        dataBuffer = "";
      }
      std::string remainingData = DTSCPart["data"].asString();
      typeFlag = 0;
      while (remainingData.size() > 255*255){
        //output part of the segment
        //granule -1
        curSegTable.clear();
        curSegTable.push_back(255*255);
        retVal.clear();
        retVal.setVersion();
        retVal.setHeaderType(typeFlag);//normal Page
        retVal.setGranulePosition(-1);
        retVal.setBitstreamSerialNumber(trackInf[DTSCID].OGGSerial);
        retVal.setPageSequenceNumber(trackInf[DTSCID].seqNum);
        retVal.setSegmentTable(curSegTable);
        retVal.setPayload((char*)remainingData.substr(0,255*255).c_str(), 255*255);
        retVal.setCRCChecksum(retVal.calcChecksum());
        trackInf[DTSCID].seqNum++;
        pageBuffer += std::string((char*)retVal.getPage(), retVal.getPageSize());
        remainingData = remainingData.substr(255*255);
        typeFlag = 1;//1 = continued page
      }
      //output last remaining data
      curSegTable.clear();
      curSegTable.push_back(remainingData.size());
      dataBuffer += remainingData;
    }else{//build data for page
      curSegTable.push_back(DTSCPart["data"].asString().size());
      dataBuffer += DTSCPart["data"].asString();
    }
    //calculating granule position
    if (trackInf[DTSCID].codec == "theora"){
      if (DTSCPart["keyframe"].asBool()){
        trackInf[DTSCID].lastKeyFrame += trackInf[DTSCID].sinceKeyFrame + 1;
        trackInf[DTSCID].sinceKeyFrame = 0;
      }else{
        trackInf[DTSCID].sinceKeyFrame ++;
      }
      lastGran = (trackInf[DTSCID].lastKeyFrame << trackInf[DTSCID].significantValue) + trackInf[DTSCID].sinceKeyFrame;
    } else if (trackInf[DTSCID].codec == "vorbis"){
      Utils::bitstreamLSBF packet;
      packet.append(DTSCPart["data"].asString());
      //calculate amount of samples associated with that block (from ID header)
      //check mode block in deque for index
      int curPCMSamples = 0;
      if (packet.get(1) == 0){
        int tempModes = vorbis::ilog(trackInf[DTSCID].vorbisModes.size()-1);
        int tempPacket = packet.get(tempModes);
        int curBlockFlag = trackInf[DTSCID].vorbisModes[tempPacket].blockFlag;
        curPCMSamples = (1 << trackInf[DTSCID].blockSize[curBlockFlag]);
        if (trackInf[DTSCID].prevBlockFlag!= -1){
          if (curBlockFlag == trackInf[DTSCID].prevBlockFlag){
            curPCMSamples /= 2;
          }else{
            curPCMSamples -= (1 << trackInf[DTSCID].blockSize[0]) / 4 + (1 << trackInf[DTSCID].blockSize[1]) / 4;
          }
        }
        trackInf[DTSCID].sinceKeyFrame = (1 << trackInf[DTSCID].blockSize[curBlockFlag]);
        trackInf[DTSCID].prevBlockFlag = curBlockFlag;
      }else{
        std::cerr << "Error, Vorbis packet type !=0" << std::endl;
      }
      //add to granule position
      trackInf[DTSCID].lastKeyFrame += curPCMSamples;
      lastGran  = trackInf[DTSCID].lastKeyFrame;
    } else if (trackInf[DTSCID].codec == "opus"){
      lastGran = (int)((DTSCPart["time"].asInt() * 48.0) / 120.0 + 0.5) * 120;
    }
    //}
    //last parts of page put out 
    if (!curSegTable.empty()){
      retVal.clear();
      retVal.setVersion();
      retVal.setHeaderType(typeFlag);//headertype 0 = normal
      retVal.setGranulePosition(lastGran);
      retVal.setBitstreamSerialNumber(trackInf[DTSCID].OGGSerial);
      retVal.setPageSequenceNumber(trackInf[DTSCID].seqNum);
      retVal.setSegmentTable(curSegTable);
      retVal.setPayload((char*)dataBuffer.c_str(), dataBuffer.size());
      retVal.setCRCChecksum(retVal.calcChecksum());
      trackInf[DTSCID].seqNum++;
      pageBuffer += std::string((char*)retVal.getPage(), retVal.getPageSize());
    }
  }
}
