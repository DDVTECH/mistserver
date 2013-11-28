#include "mp4.h"
#include <sstream>

namespace MP4{
  /*bool keyPartSort(keyPart i, keyPart j){
    return (i.time < j.time);
  }*/

  std::string DTSC2MP4Converter::DTSCMeta2MP4Header(DTSC::Meta & metaData){
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
    MP4::MOOV moovBox;
      MP4::MVHD mvhdBox;
      mvhdBox.setVersion(0);
      mvhdBox.setCreationTime(0);
      mvhdBox.setModificationTime(0);
      mvhdBox.setTimeScale(1000);
      mvhdBox.setRate(0x10000);
      //calculating longest duration
      int fileDuration = 0;
      ///\TODO lastms and firstms fixen
      for ( std::map<int,DTSC::Track>::iterator trackIt = metaData.tracks.begin(); trackIt != metaData.tracks.end(); trackIt ++) {
        if (trackIt->second.lastms - trackIt->second.firstms > fileDuration){
          fileDuration =  trackIt->second.lastms - trackIt->second.firstms;
        }
      }
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
      
      //calculate interleaving
      //putting all metadata in a huge, auto-sorting vector 'keyParts'
      keyParts.clear();
      for ( std::map<int,DTSC::Track>::iterator trackIt = metaData.tracks.begin(); trackIt != metaData.tracks.end(); trackIt ++) {
        if (trackIt->first>0){
          std::cerr << "preparing track: " << trackIt->first << std::endl;
          int partItNumber = 0;
          for ( std::deque< DTSC::Key>::iterator keyIt = trackIt->second.keys.begin(); keyIt != trackIt->second.keys.end(); keyIt ++) {
            //if ((*keyIt)->size > 0){
              keyPart temp;
              temp.trackID = trackIt->second.trackID;
              /*
              temp.size = (*keyIt)["size"].asInt();
              temp.time = (*keyIt)["time"].asInt();
              temp.len = (*keyIt)["len"].asInt();
              temp.parts = (*keyIt)["parts"].asString();
              temp.partsize = (*keyIt)["partsize"].asInt();
              */
              temp.time = keyIt->getTime();//timeplaats van keyframe
              std::cerr << "time: " << temp.time << std::endl;
              temp.len = keyIt->getLength();//duration van keyframe
              //std::cerr << "totalparts, partItNumber, getparts:"<< trackIt->second.parts.size() << ", " << partItNumber << ", " << keyIt->getParts() << std::endl;
              temp.parts = std::deque<DTSC::Part> (trackIt->second.parts.begin() + partItNumber,trackIt->second.parts.begin() + partItNumber + keyIt->getParts() );//array met bytegrootte van elke aparte part
              //calculate total size of parts
              int tempSize = 0;
              //std::cerr << "keyframe parts: ";
              for (unsigned int di = 0; di < temp.parts.size(); di++){
                tempSize += temp.parts[di].getSize();
                //std::cerr << temp.parts[di].getSize() << " ";
              }
              //std::cerr << std::endl;
              temp.size = tempSize;//bytegrootte van keyframe (alle parts bij elkaar)
              temp.partsize = keyIt->getParts();//amount of parts in this keyframe
              
              keyParts.insert(temp);
            //}
            partItNumber += keyIt->getParts();
          }
        }
      }
      //sort by time on keyframes for interleaving
      //std::sort(keyParts.begin(), keyParts.end(), keyPartSort);
      
      //start arbitrary track addition for header
      int boxOffset = 1;
      for ( std::map<int,DTSC::Track>::iterator it = metaData.tracks.begin(); it != metaData.tracks.end(); it ++) {
        if (it->first > 0){
        std::cerr << "track " << it->second.trackID << std::endl;
      //for (JSON::ObjIter it = metaData["tracks"].ObjBegin(); it != metaData["tracks"].ObjEnd(); it++){
        int timescale = 0;
        MP4::TRAK trakBox;
          MP4::TKHD tkhdBox;
          tkhdBox.setVersion(0);
          tkhdBox.setFlags(15);
          tkhdBox.setTrackID(it->second.trackID);
          ///\TODO duration firstms and lastms fix
          tkhdBox.setDuration(it->second.lastms + it->second.firstms);
          
          if (it->second.type == "video"){
            tkhdBox.setWidth(it->second.width << 16);
            tkhdBox.setHeight(it->second.height << 16);
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
          
          MP4::MDIA mdiaBox;
            MP4::MDHD mdhdBox(0);/// \todo fix constructor mdhd in lib
            mdhdBox.setCreationTime(0);
            mdhdBox.setModificationTime(0);
            //Calculating media time based on sampledelta. Probably cheating, but it works...
            int tmpParts = 0;
            for (std::deque< DTSC::Key>::iterator tmpIt = it->second.keys.begin(); tmpIt != it->second.keys.end(); tmpIt ++) {
            //for (JSON::ArrIter tmpIt = it->second["keys"].ArrBegin(); tmpIt != it->second["keys"].ArrEnd(); tmpIt++){
              tmpParts += tmpIt->getParts();
            }
            timescale = ((double)(42 * tmpParts) / (it->second.lastms + it->second.firstms)) *  1000;
            mdhdBox.setTimeScale(timescale);
            ///\TODO fix lastms, firstms
            mdhdBox.setDuration((it->second.lastms + it->second.firstms) * ((double)timescale / 1000));
            mdiaBox.setContent(mdhdBox, 0);
            
            std::string tmpStr = it->second.type;
            MP4::HDLR hdlrBox;/// \todo fix constructor hdlr in lib
            if (tmpStr == "video"){
              hdlrBox.setHandlerType(0x76696465);//vide
            }else if (tmpStr == "audio"){
              hdlrBox.setHandlerType(0x736F756E);//soun
            }
            hdlrBox.setName(it->second.getIdentifier());
            mdiaBox.setContent(hdlrBox, 1);
            
            MP4::MINF minfBox;
              if (tmpStr == "video"){
                MP4::VMHD vmhdBox;
                vmhdBox.setFlags(1);
                minfBox.setContent(vmhdBox,0);
              }else if (tmpStr == "audio"){
                MP4::SMHD smhdBox;
                minfBox.setContent(smhdBox,0);
              }
              MP4::DINF dinfBox;
                MP4::DREF drefBox;/// \todo fix constructor dref in lib
                  drefBox.setVersion(0);
                  MP4::URL urlBox;
                  urlBox.setFlags(1);
                  drefBox.setDataEntry(urlBox,0);
                dinfBox.setContent(drefBox,0);
              minfBox.setContent(dinfBox,1);
              
              MP4::STBL stblBox;
                MP4::STSD stsdBox;
                  stsdBox.setVersion(0);
                  if (tmpStr == "video"){//boxname = codec
                    MP4::VisualSampleEntry vse;
                    std::string tmpStr2 = it->second.codec;
                    if (tmpStr2 == "H264"){
                      vse.setCodec("avc1");
                    }
                    vse.setDataReferenceIndex(1);
                    vse.setWidth(it->second.width);
                    vse.setHeight(it->second.height);
                      MP4::AVCC avccBox;
                      avccBox.setPayload(it->second.init);
                      vse.setCLAP(avccBox);
                    stsdBox.setEntry(vse,0);
                  }else if(tmpStr == "audio"){//boxname = codec
                    MP4::AudioSampleEntry ase;
                    std::string tmpStr2 = it->second.codec;
                    if (tmpStr2 == "AAC"){
                      ase.setCodec("mp4a");
                      ase.setDataReferenceIndex(1);
                    }
                    ase.setSampleRate(it->second.rate);
                    ase.setChannelCount(it->second.channels);
                    ase.setSampleSize(it->second.size);
                      MP4::ESDS esdsBox;
                      esdsBox.setESDescriptorTypeLength(32+it->second.init.size());
                      esdsBox.setESID(2);
                      esdsBox.setStreamPriority(0);
                      esdsBox.setDecoderConfigDescriptorTypeLength(18+it->second.init.size());
                      esdsBox.setByteObjectTypeID(0x40);
                      esdsBox.setStreamType(5);
                      esdsBox.setReservedFlag(1);
                      esdsBox.setBufferSize(1250000);
                      esdsBox.setMaximumBitRate(10000000);
                      esdsBox.setAverageBitRate(it->second.bps * 8);
                      esdsBox.setConfigDescriptorTypeLength(5);
                      esdsBox.setESHeaderStartCodes(it->second.init);
                      esdsBox.setSLConfigDescriptorTypeTag(0x6);
                      esdsBox.setSLConfigExtendedDescriptorTypeTag(0x808080);
                      esdsBox.setSLDescriptorTypeLength(1);
                      esdsBox.setSLValue(2);
                      ase.setCodecBox(esdsBox);
                    stsdBox.setEntry(ase,0);
                  }
                stblBox.setContent(stsdBox,0);
                
                /// \todo update following stts lines
                MP4::STTS sttsBox;//current version probably causes problems
                  sttsBox.setVersion(0);
                  MP4::STTSEntry newEntry;
                  newEntry.sampleCount = tmpParts;
                  //42, Used as magic number for timescale calculation
                  newEntry.sampleDelta = 42;
                  sttsBox.setSTTSEntry(newEntry, 0);
                stblBox.setContent(sttsBox,1);
                
                if (it->second.type == "video"){
                  //STSS Box here
                  MP4::STSS stssBox;
                    stssBox.setVersion(0);
                    int tmpCount = 1;
                    int tmpItCount = 0;
                    for ( std::deque< DTSC::Key>::iterator tmpIt = it->second.keys.begin(); tmpIt != it->second.keys.end(); tmpIt ++) {
                      stssBox.setSampleNumber(tmpCount,tmpItCount);
                      tmpCount += tmpIt->getParts();
                      tmpItCount ++;
                    }
                  stblBox.setContent(stssBox,2);
                }

                int offset = (it->second.type == "video");

                
                MP4::STSC stscBox;
                stscBox.setVersion(0);
                uint32_t total = 0;
                MP4::STSCEntry stscEntry;
                stscEntry.firstChunk = 1;
                stscEntry.samplesPerChunk = 1;
                stscEntry.sampleDescriptionIndex = 1;
                stscBox.setSTSCEntry(stscEntry, 0);
                stblBox.setContent(stscBox,2 + offset);

                MP4::STSZ stszBox;
                stszBox.setVersion(0);
                total = 0;
                for (std::deque< DTSC::Part>::iterator partIt = it->second.parts.begin(); partIt != it->second.parts.end(); partIt ++) {
                //for (int i = 0; i < it->second["keys"].size(); i++){
                  //std::deque<long long unsigned int> parsedParts;
                  //JSON::decodeVector(it->second["keys"][i]["parts"].asString(), parsedParts);
                  //for (unsigned int o = 0; o < tmpIt->parts.size(); o++){
                  stszBox.setEntrySize(partIt->getSize(), total);//in bytes in file
                  total++;
                }
                stblBox.setContent(stszBox,3 + offset);
                  
                MP4::STCO stcoBox;
                stcoBox.setVersion(1);
                total = 0;
                long long unsigned int totalByteOffset = 0;
                //Inserting wrong values on purpose here, will be fixed later.
                //Current values are actual byte offset without header-sized offset
                std::cerr << "pre-totalByteOffset: " << totalByteOffset << std::endl;
                for (std::set<keyPart>::iterator i = keyParts.begin(); i != keyParts.end(); i++){//for all keypart size
                  if(i->trackID == it->second.trackID){//if keypart is of current trackID
                    //std::deque<long long unsigned int> parsedParts;
                    //JSON::decodeVector(i->parts, parsedParts);
                    std::deque<DTSC::Part> onowai = i->parts;
                    for (unsigned int o = 0; o < onowai.size(); o++){//add all parts to STCO
                    //for (std::deque<DTSC::Part>::iterator partIt = (*i).parts.begin(); partIt != (*i).parts.end(); partIt++){
                      stcoBox.setChunkOffset(totalByteOffset, total);
                      total++;
                      totalByteOffset += onowai[o].getSize();
                      std::cerr << "small totalByteOffset: " << totalByteOffset << std::endl;
                    }
                  }else{
                    totalByteOffset += i->size;
                    std::cerr << "large totalByteOffset: " << totalByteOffset << std::endl;
                  }
                }
                //calculating the offset where the STCO box will be in the main MOOV box
                //needed for probable optimise
                mdatSize = totalByteOffset;
                
                stblBox.setContent(stcoBox,4 + offset);
              minfBox.setContent(stblBox,2);
            mdiaBox.setContent(minfBox, 2);
          trakBox.setContent(mdiaBox, 1);
        moovBox.setContent(trakBox, boxOffset);
        boxOffset++;
        }
      }
    //end arbitrary
    //initial offset length ftyp, length moov + 8
    unsigned long long int byteOffset = ftypBox.boxedSize() + moovBox.boxedSize() + 8;
    //update all STCO
    //for tracks
    for (unsigned int i = 1; i < moovBox.getContentCount(); i++){
      //10 lines to get the STCO box.
      MP4::TRAK checkTrakBox;
      MP4::MDIA checkMdiaBox;
      MP4::MINF checkMinfBox;
      MP4::STBL checkStblBox;
      MP4::STCO checkStcoBox;
      checkTrakBox = ((MP4::TRAK&)moovBox.getContent(i));
      for (int j = 0; j < checkTrakBox.getContentCount(); j++){
        if (checkTrakBox.getContent(j).isType("mdia")){
          checkMdiaBox = ((MP4::MDIA&)checkTrakBox.getContent(j));
          break;
        }
      }
      for (int j = 0; j < checkMdiaBox.getContentCount(); j++){
        if (checkMdiaBox.getContent(j).isType("minf")){
          checkMinfBox = ((MP4::MINF&)checkMdiaBox.getContent(j));
          break;
        }
      }
      for (int j = 0; j < checkMinfBox.getContentCount(); j++){
        if (checkMinfBox.getContent(j).isType("stbl")){
          checkStblBox = ((MP4::STBL&)checkMinfBox.getContent(j));
          break;
        }
      }
      for (int j = 0; j < checkStblBox.getContentCount(); j++){
        if (checkStblBox.getContent(j).isType("stco")){
          checkStcoBox = ((MP4::STCO&)checkStblBox.getContent(j));
          break;
        }
      }
      //got the STCO box, fixing values with MP4 header offset
      for (int j = 0; j < checkStcoBox.getEntryCount(); j++){
        checkStcoBox.setChunkOffset(checkStcoBox.getChunkOffset(j) + byteOffset, j);
      }
    }
    header << std::string(moovBox.asBox(),moovBox.boxedSize());

    header << (char)((mdatSize>>24) & 0x000000FF) << (char)((mdatSize>>16) & 0x000000FF) << (char)((mdatSize>>8) & 0x000000FF) << (char)(mdatSize & 0x000000FF) << "mdat";
    //end of header
    
    return header.str();
  }
  
  void DTSC2MP4Converter::parseDTSC(JSON::Value mediaPart){
    static std::set<keyPart>::iterator curKey = keyParts.begin();//the key chunk we are currently searching for in keyParts
    static long long unsigned int curPart = 0;//current part in current key
    //mdat output here
    //output cleanout buffer first
    //while there are requested packets in the trackBuffer:...
    while (!trackBuffer[curKey->trackID].empty()){
      //output requested packages
      //std::deque<DTSC::Part> onowai = curKey->parts;
      stringBuffer += trackBuffer[curKey->trackID].front()["data"].asString();
      //std::cerr << "bufDataSize, antDataSize" << trackBuffer[curKey->trackID].front()["data"].asString().size() << ", " << onowai[curPart].getSize() << std::endl;
      trackBuffer[curKey->trackID].pop_front();
      curPart++;
      if(curPart >= curKey->parts.size()){
        curPart = 0;
        curKey++;
      }
    }
    //after that, try to put out the JSON data directly
    if(curKey->trackID == mediaPart["trackid"].asInt()){
      //output JSON packet
      stringBuffer += mediaPart["data"].asStringRef();
      //std::deque<DTSC::Part> onowai = curKey->parts;
      //std::cerr << "dataSize, antDataSize" << mediaPart["data"].asStringRef().size() << ", " << onowai[curPart].getSize() << std::endl;
      curPart++;
      if(curPart >= curKey->parts.size()){
        curPart = 0;
        curKey++;
      }
    }else{
      //buffer for later
      trackBuffer[mediaPart["trackid"].asInt()].push_back(mediaPart);
    }
  }

  bool DTSC2MP4Converter::sendReady(){
    if (stringBuffer.length() > 0){
      return true;
    }else{
      return false;
    }
  }
  
  std::string DTSC2MP4Converter::sendString(){
    std::string temp = stringBuffer;
    stringBuffer = "";
    return temp;
  }

  std::string DTSC2MP4Converter::purgeBuffer(){
    std::string retval = stringBuffer;
    stringBuffer = "";
    for (std::map <long long unsigned int, std::deque<JSON::Value> >::iterator it = trackBuffer.begin(); it !=trackBuffer.end(); it++){
      while (!it->second.empty()){
        //output requested packages
        if (it->second.front()["data"].asString() != ""){
          retval += it->second.front()["data"].asString();
        }
        it->second.pop_front();
      }
    }
    return retval;
  }
}

