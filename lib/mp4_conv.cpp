#include "mp4_generic.h"
#include <sstream>

namespace MP4{
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
    MP4::MOOV moovBox;{
      //calculating longest duration
      long long int fileDuration = 0;
      /// \todo lastms and firstms fix
      for ( std::map<int,DTSC::Track>::iterator trackIt = metaData.tracks.begin(); trackIt != metaData.tracks.end(); trackIt ++) {
        if (trackIt->second.lastms - trackIt->second.firstms > fileDuration){
          fileDuration =  trackIt->second.lastms - trackIt->second.firstms;
        }
      }
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
    {//start arbitrary track addition for header
      int boxOffset = 1;
      bool seenAudio = false;
      bool seenVideo = false;
      for ( std::map<int,DTSC::Track>::iterator it = metaData.tracks.begin(); it != metaData.tracks.end(); it ++) {
        if (it->second.codec != "AAC" && it->second.codec != "H264"){continue;}
        if (it->second.type == "audio"){
          if (seenAudio){continue;}
          seenAudio = true;
        }
        if (it->second.type == "video"){
          if (seenVideo){continue;}
          seenVideo = true;
        }
        if (it->first > 0){
          int timescale = 0;
          MP4::TRAK trakBox;
          {
            {
            MP4::TKHD tkhdBox;
            tkhdBox.setVersion(0);
            tkhdBox.setFlags(15);
            tkhdBox.setTrackID(it->second.trackID);
            /// \todo duration firstms and lastms fix
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
            }{
            MP4::MDIA mdiaBox;
              {
              MP4::MDHD mdhdBox(0);/// \todo fix constructor mdhd in lib
              mdhdBox.setCreationTime(0);
              mdhdBox.setModificationTime(0);
              //Calculating media time based on sampledelta. Probably cheating, but it works...
              timescale = ((double)(42 * it->second.parts.size() ) / (it->second.lastms + it->second.firstms)) *  1000;
              mdhdBox.setTimeScale(timescale);
              /// \todo fix lastms, firstms
              mdhdBox.setDuration((it->second.lastms + it->second.firstms) * ((double)timescale / 1000));
              mdiaBox.setContent(mdhdBox, 0);
              }//MDHD box
              {
              MP4::HDLR hdlrBox;/// \todo fix constructor hdlr in lib
              if (it->second.type == "video"){
                hdlrBox.setHandlerType(0x76696465);//vide
              }else if (it->second.type == "audio"){
                hdlrBox.setHandlerType(0x736F756E);//soun
              }
              hdlrBox.setName(it->second.getIdentifier());
              mdiaBox.setContent(hdlrBox, 1);
              }//hdlr box
              {
              MP4::MINF minfBox;
                if (it->second.type== "video"){
                  MP4::VMHD vmhdBox;
                  vmhdBox.setFlags(1);
                  minfBox.setContent(vmhdBox,0);
                }else if (it->second.type == "audio"){
                  MP4::SMHD smhdBox;
                  minfBox.setContent(smhdBox,0);
                }//type box
                {
                MP4::DINF dinfBox;
                  MP4::DREF drefBox;/// \todo fix constructor dref in lib
                    drefBox.setVersion(0);
                    MP4::URL urlBox;
                    urlBox.setFlags(1);
                    drefBox.setDataEntry(urlBox,0);
                  dinfBox.setContent(drefBox,0);
                minfBox.setContent(dinfBox,1);
                }//dinf box
                {
                MP4::STBL stblBox;
                  {
                  MP4::STSD stsdBox;
                    stsdBox.setVersion(0);
                    if (it->second.type == "video"){//boxname = codec
                      MP4::VisualSampleEntry vse;
                      if (it->second.codec == "H264"){
                        vse.setCodec("avc1");
                      }
                      vse.setDataReferenceIndex(1);
                      vse.setWidth(it->second.width);
                      vse.setHeight(it->second.height);
                        MP4::AVCC avccBox;
                        avccBox.setPayload(it->second.init);
                        vse.setCLAP(avccBox);
                      stsdBox.setEntry(vse,0);
                    }else if(it->second.type == "audio"){//boxname = codec
                      MP4::AudioSampleEntry ase;
                      if (it->second.codec == "AAC"){
                        ase.setCodec("mp4a");
                        ase.setDataReferenceIndex(1);
                      }
                      ase.setSampleRate(it->second.rate);
                      ase.setChannelCount(it->second.channels);
                      ase.setSampleSize(it->second.size);
                        //MP4::ESDS esdsBox(it->second.init, it->second.bps);
                        MP4::ESDS esdsBox;

                        //outputting these values first, so malloc isn't called as often.
                        esdsBox.setESHeaderStartCodes(it->second.init);
                        esdsBox.setSLValue(2);

                        esdsBox.setESDescriptorTypeLength(32+it->second.init.size());
                        esdsBox.setESID(2);
                        esdsBox.setStreamPriority(0);
                        esdsBox.setDecoderConfigDescriptorTypeLength(18 + it->second.init.size());
                        esdsBox.setByteObjectTypeID(0x40);
                        esdsBox.setStreamType(5);
                        esdsBox.setReservedFlag(1);
                        esdsBox.setBufferSize(1250000);
                        esdsBox.setMaximumBitRate(10000000);
                        esdsBox.setAverageBitRate(it->second.bps * 8);
                        esdsBox.setConfigDescriptorTypeLength(5);
                        esdsBox.setSLConfigDescriptorTypeTag(0x6);
                        esdsBox.setSLConfigExtendedDescriptorTypeTag(0x808080);
                        esdsBox.setSLDescriptorTypeLength(1);
                        ase.setCodecBox(esdsBox);
                      stsdBox.setEntry(ase,0);
                    }
                  stblBox.setContent(stsdBox,0);
                  }//stsd box
                  /// \todo update following stts lines
                  {
                  MP4::STTS sttsBox;//current version probably causes problems
                    sttsBox.setVersion(0);
                    MP4::STTSEntry newEntry;
                    newEntry.sampleCount = it->second.parts.size();
                    //42, Used as magic number for timescale calculation
                    newEntry.sampleDelta = 42;
                    sttsBox.setSTTSEntry(newEntry, 0);
                  stblBox.setContent(sttsBox,1);
                  }//stts box
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
                  }//stss box

                  int offset = (it->second.type == "video");
                  {
                  MP4::STSC stscBox;
                  stscBox.setVersion(0);
                  MP4::STSCEntry stscEntry;
                  stscEntry.firstChunk = 1;
                  stscEntry.samplesPerChunk = 1;
                  stscEntry.sampleDescriptionIndex = 1;
                  stscBox.setSTSCEntry(stscEntry, 0);
                  stblBox.setContent(stscBox,2 + offset);
                  }//stsc box
                  {
                  uint32_t total = 0;
                  MP4::STSZ stszBox;
                  stszBox.setVersion(0);
                  total = 0;
                  for (std::deque< DTSC::Part>::iterator partIt = it->second.parts.begin(); partIt != it->second.parts.end(); partIt ++) {
                    stszBox.setEntrySize(partIt->getSize(), total);//in bytes in file
                    total++;
                  }
                  stblBox.setContent(stszBox,3 + offset);
                  }//stsz box
                  //add STCO boxes here
                  {
                  MP4::STCO stcoBox;
                  stcoBox.setVersion(1);
                  //Inserting empty values on purpose here, will be fixed later.
                  if (it->second.parts.size() != 0){
                    stcoBox.setChunkOffset(0, it->second.parts.size() - 1);//this inserts all empty entries at once
                  }
                  stblBox.setContent(stcoBox,4 + offset);
                  }//stco box
                minfBox.setContent(stblBox,2);
                }//stbl box
              mdiaBox.setContent(minfBox, 2);
              }//minf box
            trakBox.setContent(mdiaBox, 1);
            }
          }//trak Box
          moovBox.setContent(trakBox, boxOffset);
          boxOffset++;
        }
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
      MP4::MINF checkMinfBox;
      MP4::STBL checkStblBox;
      //MP4::STCO checkStcoBox;
      checkTrakBox = ((MP4::TRAK&)moovBox.getContent(i));
      for (unsigned int j = 0; j < checkTrakBox.getContentCount(); j++){
        if (checkTrakBox.getContent(j).isType("mdia")){
          checkMdiaBox = ((MP4::MDIA&)checkTrakBox.getContent(j));
          break;
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
          checkStcoBoxes.insert( std::pair<int, MP4::STCO>(i, ((MP4::STCO&)checkStblBox.getContent(j)) ));
          break;
        }
      }
    }
    //inserting right values in the STCO box header
    //total = 0;
    long long unsigned int totalByteOffset = 0;
    //Current values are actual byte offset without header-sized offset
    std::set <keyPart> sortSet;//filling sortset for interleaving parts
    for ( std::map<int,DTSC::Track>::iterator subIt = metaData.tracks.begin(); subIt != metaData.tracks.end(); subIt ++) {
      keyPart temp;
      temp.trackID = subIt->second.trackID;
      temp.time = subIt->second.firstms;//timeplace of frame
      temp.endTime = subIt->second.firstms + subIt->second.parts[0].getDuration();
      temp.size = subIt->second.parts[0].getSize();//bytesize of frame (alle parts all together)
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
    
    return header.str();
  }
  
}

