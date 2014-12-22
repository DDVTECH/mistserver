#include "output_progressive_mp4.h"
#include <mist/defines.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
#include <mist/checksum.h>

namespace Mist {
  OutProgressiveMP4::OutProgressiveMP4(Socket::Connection & conn) : HTTPOutput(conn){
    completeKeysOnly = true;
  }
  OutProgressiveMP4::~OutProgressiveMP4() {}
  
  void OutProgressiveMP4::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "MP4";
    capa["desc"] = "Enables HTTP protocol progressive streaming.";
    capa["url_rel"] = "/$.mp4";
    capa["url_match"] = "/$.mp4";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/mp4";
    capa["methods"][0u]["priority"] = 8ll;
    capa["methods"][0u]["nolive"] = 1;
  }
  
  std::string OutProgressiveMP4::DTSCMeta2MP4Header(long long & size, int fragmented){
    std::stringstream header;
    //ftyp box
    MP4::FTYP ftypBox;
    header.write(ftypBox.asBox(),ftypBox.boxedSize());
    
    uint64_t mdatSize = 0;
    
    //moov box
    MP4::MOOV moovBox;
    if (fragmented == 2){
      //change moov to moof; dirty, but efficient
    }
    unsigned int moovOffset = 0;

    long long int firstms = -1;
    long long int lastms = -1;
    MP4::MVHD mvhdBox(0);
    if (fragmented == 0){
      //calculating longest duration
      for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
        if (lastms == -1 || lastms < (long long)myMeta.tracks[*it].lastms){
          lastms = myMeta.tracks[*it].lastms;
        }
        if (firstms == -1 || firstms > (long long)myMeta.tracks[*it].firstms){
          firstms = myMeta.tracks[*it].firstms;
        }
      }
      mvhdBox.setDuration(lastms - firstms);
    }else{
      mvhdBox.setDuration(-1);
    }
    mvhdBox.setTrackID(selectedTracks.size());
    moovBox.setContent(mvhdBox, moovOffset++);
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
      DTSC::Track & thisTrack = myMeta.tracks[*it];
      MP4::TRAK trakBox;
      {
        {//fix tfhd here
          MP4::TKHD tkhdBox(*it, -1, thisTrack.width, thisTrack.height);
          if (fragmented == 0){
            tkhdBox.setDuration(thisTrack.lastms - thisTrack.firstms);
          }
          trakBox.setContent(tkhdBox, 0);
        }{
          MP4::MDIA mdiaBox;
          unsigned int mdiaOffset = 0;
          {
            MP4::MDHD mdhdBox(thisTrack.lastms - thisTrack.firstms);
            mdiaBox.setContent(mdhdBox, mdiaOffset++);
          }//MDHD box
          {
            MP4::HDLR hdlrBox(thisTrack.type, thisTrack.getIdentifier());
            mdiaBox.setContent(hdlrBox, mdiaOffset++);
          }//hdlr box
          {
            MP4::MINF minfBox;
            unsigned int minfOffset = 0;
            if (thisTrack.type== "video"){
              MP4::VMHD vmhdBox;
              vmhdBox.setFlags(1);
              minfBox.setContent(vmhdBox,minfOffset++);
            }else if (thisTrack.type == "audio"){
              MP4::SMHD smhdBox;
              minfBox.setContent(smhdBox,minfOffset++);
            }//type box
            {
              MP4::DINF dinfBox;
              MP4::DREF drefBox;
              dinfBox.setContent(drefBox,0);
              minfBox.setContent(dinfBox,minfOffset++);
            }//dinf box
            {
              MP4::STBL stblBox;//all in stbl is empty when fragmented
              unsigned int offset = 0;
              {
                MP4::STSD stsdBox;
                stsdBox.setVersion(0);
                if (thisTrack.type == "video"){//boxname = codec
                  MP4::VisualSampleEntry vse;
                  if (thisTrack.codec == "H264"){
                    vse.setCodec("avc1");
                  }
                  if (thisTrack.codec == "HEVC"){
                    vse.setCodec("hev1");
                  }
                  vse.setDataReferenceIndex(1);
                  vse.setWidth(thisTrack.width);
                  vse.setHeight(thisTrack.height);
                  if (thisTrack.codec == "H264"){
                    MP4::AVCC avccBox;
                    avccBox.setPayload(thisTrack.init);
                    vse.setCLAP(avccBox);
                  }
                  /*LTS-START*/
                  if (thisTrack.codec == "HEVC"){
                    MP4::HVCC hvccBox;
                    hvccBox.setPayload(thisTrack.init);
                    vse.setCLAP(hvccBox);
                  }
                  /*LTS-END*/
                  stsdBox.setEntry(vse,0);
                }else if(thisTrack.type == "audio"){//boxname = codec
                  MP4::AudioSampleEntry ase;
                  if (thisTrack.codec == "AAC"){
                    ase.setCodec("mp4a");
                    ase.setDataReferenceIndex(1);
                  }else if (thisTrack.codec == "MP3"){
                    ase.setCodec("mp4a");
                    ase.setDataReferenceIndex(1);
                  }
                  ase.setSampleRate(thisTrack.rate);
                  ase.setChannelCount(thisTrack.channels);
                  ase.setSampleSize(thisTrack.size);
                  //MP4::ESDS esdsBox(thisTrack.init, thisTrack.bps);
                  MP4::ESDS esdsBox;
                  
                  //outputting these values first, so malloc isn't called as often.
                  if (thisTrack.codec == "MP3"){
                    esdsBox.setESHeaderStartCodes("\002");
                    esdsBox.setConfigDescriptorTypeLength(1);
                    esdsBox.setSLConfigExtendedDescriptorTypeTag(0);
                    esdsBox.setSLDescriptorTypeLength(0);
                    esdsBox.setESDescriptorTypeLength(27);
                    esdsBox.setSLConfigDescriptorTypeTag(0);
                    esdsBox.setDecoderDescriptorTypeTag(0x06);
                    esdsBox.setSLValue(0);
                    //esdsBox.setBufferSize(0);
                    esdsBox.setDecoderConfigDescriptorTypeLength(13);
                    esdsBox.setByteObjectTypeID(0x6b);
                  }else{
                    //AAC
                    esdsBox.setESHeaderStartCodes(thisTrack.init);
                    esdsBox.setConfigDescriptorTypeLength(thisTrack.init.size());
                    esdsBox.setSLConfigExtendedDescriptorTypeTag(0x808080);
                    esdsBox.setSLDescriptorTypeLength(1);
                    esdsBox.setESDescriptorTypeLength(32+thisTrack.init.size());
                    esdsBox.setSLConfigDescriptorTypeTag(0x6);
                    esdsBox.setSLValue(2);
                    esdsBox.setDecoderConfigDescriptorTypeLength(18 + thisTrack.init.size());
                    esdsBox.setByteObjectTypeID(0x40);
                  }
                  esdsBox.setESID(2);
                  esdsBox.setStreamPriority(0);
                  esdsBox.setStreamType(5);
                  esdsBox.setReservedFlag(1);
                  esdsBox.setMaximumBitRate(10000000);
                  esdsBox.setAverageBitRate(thisTrack.bps * 8);
                  esdsBox.setBufferSize(1250000);
                  ase.setCodecBox(esdsBox);
                  stsdBox.setEntry(ase,0);
                }
                stblBox.setContent(stsdBox,offset++);
              }//stsd box
              {
                MP4::STTS sttsBox;
                sttsBox.setVersion(0);
                if (thisTrack.parts.size() && fragmented == 0){//if we are making a non fragmented MP4 and there are parts
                  for (unsigned int part = 0; part < thisTrack.parts.size(); part++){
                    MP4::STTSEntry newEntry;
                    newEntry.sampleCount = 1;
                    newEntry.sampleDelta = thisTrack.parts[part].getDuration();
                    sttsBox.setSTTSEntry(newEntry, part);
                  }
                }
                stblBox.setContent(sttsBox,offset++);
              }//stts box
              if (thisTrack.type == "video" && fragmented == 0){//if we are making a non fragmented MP4 and there are parts
                //STSS Box here
                MP4::STSS stssBox;
                stssBox.setVersion(0);
                int tmpCount = 0;
                int tmpItCount = 0;
                for ( std::deque< DTSC::Key>::iterator tmpIt = thisTrack.keys.begin(); tmpIt != thisTrack.keys.end(); tmpIt ++) {
                  stssBox.setSampleNumber(tmpCount,tmpItCount);
                  tmpCount += tmpIt->getParts();
                  tmpItCount ++;
                }
                stblBox.setContent(stssBox,offset++);
              }//stss box
              {
                MP4::STSC stscBox;
                stscBox.setVersion(0);
                if (fragmented == 0){//if we are making a non fragmented MP4 and there are parts
                  MP4::STSCEntry stscEntry;
                  stscEntry.firstChunk = 1;
                  stscEntry.samplesPerChunk = 1;
                  stscEntry.sampleDescriptionIndex = 1;
                  stscBox.setSTSCEntry(stscEntry, 0);
                }
                stblBox.setContent(stscBox,offset++);
              }//stsc box
              {
                MP4::STSZ stszBox;
                stszBox.setVersion(0);
                if (fragmented == 0){//if we are making a non fragmented MP4 and there are parts
                if (thisTrack.parts.size()){
                  std::deque<DTSC::Part>::reverse_iterator tmpIt = thisTrack.parts.rbegin();
                  for (unsigned int part = thisTrack.parts.size(); part > 0; --part){
                    unsigned int partSize = tmpIt->getSize();
                    tmpIt++;
                    stszBox.setEntrySize(partSize, part-1);//in bytes in file
                    size += partSize;
                  }
                }
                }
                stblBox.setContent(stszBox,offset++);
              }//stsz box
              {
                MP4::STCO stcoBox;
                if (fragmented == 0){//if we are making a non fragmented MP4 and there are parts
                  stcoBox.setVersion(1);
                  //Inserting empty values on purpose here, will be fixed later.
                  if (thisTrack.parts.size() != 0){
                    stcoBox.setChunkOffset(0, thisTrack.parts.size() - 1);//this inserts all empty entries at once
                  }
                }else{
                  stcoBox.setVersion(0);
                  stcoBox.setEntryCount(0);
                }
                stblBox.setContent(stcoBox,offset++);
              }//stco box
              minfBox.setContent(stblBox,minfOffset++);
            }//stbl box
            mdiaBox.setContent(minfBox, mdiaOffset++);
          }//minf box
          trakBox.setContent(mdiaBox, 1);
        }
      }//trak Box
      moovBox.setContent(trakBox, moovOffset++);
    }//for each selected track

    //add mvex
    if (fragmented == 1){
      MP4::MVEX mvexBox;
      unsigned int curBox = 0;
      for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
        MP4::TREX trexBox;
        trexBox.setTrackID(*it);
        trexBox.setDefaultSampleDescriptionIndex(1);
        trexBox.setDefaultSampleDuration(0);
        trexBox.setDefaultSampleSize(0);
        trexBox.setDefaultSampleFlags(0);
        mvexBox.setContent(trexBox, curBox++);
      }
      moovBox.setContent(mvexBox, moovOffset++);
    }
    if (fragmented == 0){//if we are making a non fragmented MP4 and there are parts
      //initial offset length ftyp, length moov + 8
      unsigned long long int byteOffset = ftypBox.boxedSize() + moovBox.boxedSize() + 8;
      //update all STCO from the following map;
      std::map <int, MP4::STCO> checkStcoBoxes;
      //for all tracks
      for (unsigned int i = 1; i < moovBox.getContentCount(); i++){
        //10 lines to get the STCO box.
        MP4::TRAK checkTrakBox;
        MP4::Box checkMdiaBox;
        MP4::Box checkTkhdBox;
        MP4::MINF checkMinfBox;
        MP4::STBL checkStblBox;
        //MP4::STCO checkStcoBox;
        checkTrakBox = ((MP4::TRAK&)moovBox.getContent(i));
        for (unsigned int j = 0; j < checkTrakBox.getContentCount(); j++){
          if (checkTrakBox.getContent(j).isType("mdia")){
            checkMdiaBox = checkTrakBox.getContent(j);
            break;
          }
          if (checkTrakBox.getContent(j).isType("tkhd")){
            checkTkhdBox = checkTrakBox.getContent(j);
          }
        }
        for (unsigned int j = 0; j < ((MP4::MDIA&)checkMdiaBox).getContentCount(); j++){
          if (((MP4::MDIA&)checkMdiaBox).getContent(j).isType("minf")){
            checkMinfBox = ((MP4::MINF&)((MP4::MDIA&)checkMdiaBox).getContent(j));
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
            checkStcoBoxes.insert( std::pair<int, MP4::STCO>(((MP4::TKHD&)checkTkhdBox).getTrackID(), ((MP4::STCO&)checkStblBox.getContent(j)) ));
            break;
          }
        }
      }
      //inserting right values in the STCO box header
      //total = 0;
      long long unsigned int totalByteOffset = 0;
      //Current values are actual byte offset without header-sized offset
      //std::set <keyPart> sortSet;//filling sortset for interleaving parts
      for (std::set<long unsigned int>::iterator subIt = selectedTracks.begin(); subIt != selectedTracks.end(); subIt++) {
        keyPart temp;
        temp.trackID = *subIt;
        temp.time = myMeta.tracks[*subIt].firstms;//timeplace of frame
        temp.endTime = myMeta.tracks[*subIt].firstms + myMeta.tracks[*subIt].parts[0].getDuration();
        temp.size = myMeta.tracks[*subIt].parts[0].getSize();//bytesize of frame (alle parts all together)
        temp.index = 0;
        sortSet.insert(temp);
      }
      while (!sortSet.empty()){
        std::set<keyPart>::iterator keyBegin = sortSet.begin();
        //setting the right STCO size in the STCO box
        checkStcoBoxes[keyBegin->trackID].setChunkOffset(totalByteOffset + byteOffset, sortSet.begin()->index);
        totalByteOffset += keyBegin->size;
        //add keyPart to sortSet
        keyPart temp;
        temp.index = keyBegin->index + 1;
        temp.trackID = keyBegin->trackID;
        DTSC::Track & thisTrack = myMeta.tracks[temp.trackID];
        if(temp.index < thisTrack.parts.size() ){//only insert when there are parts left
          temp.time = keyBegin->endTime;//timeplace of frame
          temp.endTime = keyBegin->endTime + thisTrack.parts[temp.index].getDuration();
          temp.size = thisTrack.parts[temp.index].getSize();//bytesize of frame 
          sortSet.insert(temp);
        }
        //remove highest keyPart
        sortSet.erase(keyBegin);
      }
      mdatSize = totalByteOffset+8;
    }//end if filling STCO and setting sortSet
    header << std::string(moovBox.asBox(),moovBox.boxedSize());
    if (fragmented == 0){//if we are making a non fragmented MP4 and there are parts
      header << (char)((mdatSize>>24) & 0xFF) << (char)((mdatSize>>16) & 0xFF) << (char)((mdatSize>>8) & 0xFF) << (char)(mdatSize & 0xFF) << "mdat";
      //end of header
    }else{
      buildFragmentDSP.clear();
      buildFragmentDSPTime.clear();
      for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
        buildFragmentDSP[*it] = 0;
        buildFragmentDSPTime[*it] = 0;
      }
      //this is a very quick fix to prevent the code from adding 0xDE to the end of the header
      //delete this code when problems occur or other appropriate times
      header << (char)(0);
    }
    
    size += header.str().size();
    if (fragmented == 1){
      realBaseOffset = header.str().size();
    }
    return header.str();
  }
  
  /// Calculate a seekPoint, based on byteStart, metadata, tracks and headerSize.
  /// The seekPoint will be set to the timestamp of the first packet to send.
  void OutProgressiveMP4::findSeekPoint(long long byteStart, long long & seekPoint, unsigned int headerSize){
    seekPoint = 0;
    //if we're starting in the header, seekPoint is always zero.
    if (byteStart <= headerSize){return;}
    //okay, we're past the header. Substract the headersize from the starting postion.
    byteStart -= headerSize;
    //forward through the file by headers, until we reach the point where we need to be
    while (!sortSet.empty()){
      //record where we are
      seekPoint = sortSet.begin()->time;
      //substract the size of this fragment from byteStart
      byteStart -= sortSet.begin()->size;
      //if that put us past the point where we wanted to be, return right now
      if (byteStart < 0){return;}
      //otherwise, set currPos to where we are now and continue
      currPos += sortSet.begin()->size;
      //find the next part
      keyPart temp;
      temp.index = sortSet.begin()->index + 1;
      temp.trackID = sortSet.begin()->trackID;
      if(temp.index < myMeta.tracks[temp.trackID].parts.size() ){//only insert when there are parts left
        temp.time = sortSet.begin()->endTime;//timeplace of frame
        temp.endTime = sortSet.begin()->endTime + myMeta.tracks[temp.trackID].parts[temp.index].getDuration();
        temp.size = myMeta.tracks[temp.trackID].parts[temp.index].getSize();//bytesize of frame 
        sortSet.insert(temp);
      }
      //remove highest keyPart
      sortSet.erase(sortSet.begin());
      //wash, rinse, repeat
    }
    //If we're here, we're in the last fragment.
    //That's technically legal, of course.
  }
  
  /// Parses a "Range: " header, setting byteStart, byteEnd and seekPoint using data from metadata and tracks to do
  /// the calculations.
  /// On error, byteEnd is set to zero.
  void OutProgressiveMP4::parseRange(std::string header, long long & byteStart, long long & byteEnd, long long & seekPoint, unsigned int headerSize){
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
        findSeekPoint(byteStart, seekPoint, headerSize);
        return;
      }else{
        //start byteStart bytes before byteEnd
        byteStart = byteEnd - byteStart;
        findSeekPoint(byteStart, seekPoint, headerSize);
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
      DEBUG_MSG(DLVL_MEDIUM, "Range request: %lli-%lli (%s)", byteStart, byteEnd, header.c_str());
      findSeekPoint(byteStart, seekPoint, headerSize);
      return;
    }
  }
  
  std::string OutProgressiveMP4::fragmentHeader(int fragNum, std::map<long unsigned int, fragSet> partSet){
    long unsigned int dataOffset = 0;
    uint64_t mdatSize = 8;
    MP4::MOOF moofBox;
    MP4::MFHD mfhdBox;
    mfhdBox.setSequenceNumber (fragNum);
    moofBox.setContent(mfhdBox, 0);
    unsigned int moofIndex = 1;
    for (std::map<long unsigned int, fragSet>::iterator it = partSet.begin(); it != partSet.end(); it++){
      MP4::TRAF trafBox;
      MP4::TFHD tfhdBox;
      tfhdBox.setFlags(MP4::tfhdBaseOffset | MP4::tfhdSampleDura | MP4::tfhdSampleSize | MP4::tfhdSampleFlag);
      tfhdBox.setTrackID(it->first);
      tfhdBox.setBaseDataOffset(realBaseOffset -1);//Offset of current moof box, we use currPos for this. Not sure why we need the -1, but this gives the right offset
      tfhdBox.setDefaultSampleDuration(myMeta.tracks[it->first].parts[it->second.firstPart].getDuration());
      tfhdBox.setDefaultSampleSize(myMeta.tracks[it->first].parts[it->second.firstPart].getSize());
      if (it->first == vidTrack){
        tfhdBox.setDefaultSampleFlags(MP4::noIPicture | MP4::noKeySample);
      }else{
        tfhdBox.setDefaultSampleFlags(MP4::isIPicture | MP4::isKeySample);
      }
      trafBox.setContent(tfhdBox, 0);
      
      MP4::TRUN trunBox;
      //checking if different values
      //putting the values in
      long unsigned int nowFlag = MP4::trundataOffset | MP4::trunfirstSampleFlags | MP4::trunsampleSize | MP4::trunsampleDuration;
      trunBox.setFlags(nowFlag);//Setting flags, so the right values go into tsi
      trunBox.setDataOffset(dataOffset);//the value stored is to be changed later
      if (it->first == vidTrack){
        trunBox.setFirstSampleFlags(MP4::isIPicture | MP4::isKeySample);
      }else{
        trunBox.setFirstSampleFlags(MP4::noIPicture | MP4::noKeySample);
      }
      for (long unsigned int i = it->second.firstPart; i <= it->second.lastPart; i++){
        dataOffset += myMeta.tracks[it->first].parts[i].getSize();
        mdatSize += myMeta.tracks[it->first].parts[i].getSize();//adding size to mdatSize

        MP4::trunSampleInformation tsi;
        tsi.sampleSize = myMeta.tracks[it->first].parts[i].getSize();
        tsi.sampleDuration = myMeta.tracks[it->first].parts[i].getDuration();
        trunBox.setSampleInformation(tsi, i - it->second.firstPart);
      }
      trafBox.setContent(trunBox, 1);
      
      moofBox.setContent(trafBox, moofIndex);
      moofIndex++;
    }
    
    //now we do the following
    //moof->traf->trun->setDataOffset(getDataOffset() + fragmentHeaderSize);
    MP4::TRAF loopTrafBox;
    MP4::TRUN fixTrunBox;
    for (unsigned int i = 0; i < moofBox.getContentCount(); i++){
      if (moofBox.getContent(i).isType("traf")){
        loopTrafBox = ((MP4::TRAF&)moofBox.getContent(i));
        for (unsigned int j = 0; j < loopTrafBox.getContentCount(); j++){
          if (loopTrafBox.getContent(j).isType("trun")){
            fixTrunBox = ((MP4::TRUN&)loopTrafBox.getContent(j));
            fixTrunBox.setDataOffset(fixTrunBox.getDataOffset() + moofBox.boxedSize() + 8);
          }
        }
      }
    }
    realBaseOffset += (moofBox.boxedSize() + mdatSize);
    std::stringstream retVal;
    retVal << std::string(moofBox.asBox(),moofBox.boxedSize());
    retVal << (char)((mdatSize>>24) & 0xFF) << (char)((mdatSize>>16) & 0xFF) << (char)((mdatSize>>8) & 0xFF) << (char)(mdatSize & 0xFF) << "mdat";
    return retVal.str();
  }
  
  void OutProgressiveMP4::onHTTP(){
    /*LTS-START*/
    //allow setting of max lead time through buffer variable.
    //max lead time is set in MS, but the variable is in integer seconds for simplicity.
    if (H.GetVar("buffer") != ""){
      maxSkipAhead = JSON::Value(H.GetVar("buffer")).asInt() * 1000;
      minSkipAhead = maxSkipAhead - std::min(2500u, maxSkipAhead / 2);
    }
    /*LTS-END*/
    initialize();
    parseData = true;
    wantRequest = false;
    sentHeader = false;
    fileSize = 0;

    std::string headerData;
    seekPoint = 0;
    if (myMeta.live){
      //for live we use fragmented mode
      headerData = DTSCMeta2MP4Header(fileSize, 1);
      fragSeqNum = 0;
      partListSent = 0;
      //seek to first video keyframe here
      setvidTrack();
      //making sure we have a first keyframe
      if (vidTrack == 0 || myMeta.tracks[vidTrack].keys.size() == 0 || myMeta.tracks[vidTrack].keys.begin()->getLength() == 0){
        WARN_MSG("Stream not ready yet");
        myConn.close();
        return;
      }else{
      }
      ///\todo Note: Not necessary, but we might want to think of a method that does not use seeking
      if (myMeta.live){
        seekPoint = myMeta.tracks[vidTrack].keys.begin()->getTime();
      }
      fragKeyNumberShift = myMeta.tracks[vidTrack].keys.begin()->getNumber() - 1;
    }else{
      headerData = DTSCMeta2MP4Header(fileSize);
    }

    byteStart = 0;
    byteEnd = fileSize - 1;
    char rangeType = ' ';
    currPos = 0;
    sortSet.clear();
    for (std::set<long unsigned int>::iterator subIt = selectedTracks.begin(); subIt != selectedTracks.end(); subIt++) {
      keyPart temp;
      temp.trackID = *subIt;
      temp.time = myMeta.tracks[*subIt].firstms;//timeplace of frame
      temp.endTime = myMeta.tracks[*subIt].firstms + myMeta.tracks[*subIt].parts[0].getDuration();
      temp.size = myMeta.tracks[*subIt].parts[0].getSize();//bytesize of frame (alle parts all together)
      temp.index = 0;
      sortSet.insert(temp);
    }
    if (!myMeta.live){
      if (H.GetHeader("Range") != ""){
        parseRange(H.GetHeader("Range"), byteStart, byteEnd, seekPoint, headerData.size());
        rangeType = H.GetHeader("Range")[0];
      }
    }
    H.Clean(); //make sure no parts of old requests are left in any buffers
    H.SetHeader("Content-Type", "video/MP4"); //Send the correct content-type for MP4 files
    if (!myMeta.live){
      H.SetHeader("Accept-Ranges", "bytes, parsec");
    }
    if (rangeType != ' '){
      if (!byteEnd){
        if (rangeType == 'p'){
          H.SetBody("Starsystem not in communications range");
          H.SendResponse("416", "Starsystem not in communications range", myConn);
          return;
        }else{
          H.SetBody("Requested Range Not Satisfiable");
          H.SendResponse("416", "Requested Range Not Satisfiable", myConn);
          return;
        }
      }else{
        std::stringstream rangeReply;
        rangeReply << "bytes " << byteStart << "-" << byteEnd << "/" << fileSize;
        H.SetHeader("Content-Length", byteEnd - byteStart + 1);
        //do not multiplex requests that are > 1MiB
        if (byteEnd - byteStart + 1 > 1024*1024){
          H.SetHeader("MistMultiplex", "No");
        }
        H.SetHeader("Content-Range", rangeReply.str());
        /// \todo Switch to chunked?
        H.SendResponse("206", "Partial content", myConn);
        //H.StartResponse("206", "Partial content", H, conn);
      }
    }else{
      if (!myMeta.live){
        H.SetHeader("Content-Length", byteEnd - byteStart + 1);
      }
      //do not multiplex requests that aren't ranged
      H.SetHeader("MistMultiplex", "No");
      /// \todo Switch to chunked?
      H.SendResponse("200", "OK", myConn);
      //H.StartResponse(H, conn);
    }
    leftOver = byteEnd - byteStart + 1;//add one byte, because range "0-0" = 1 byte of data
    if (byteStart < (long long)headerData.size()){
      /// \todo Switch to chunked?
      //H.Chunkify(headerData.data()+byteStart, std::min((long long)headerData.size(), byteEnd) - byteStart, conn);//send MP4 header
      myConn.SendNow(headerData.data()+byteStart, std::min((long long)headerData.size(), byteEnd) - byteStart);//send MP4 header
      leftOver -= std::min((long long)headerData.size(), byteEnd) - byteStart;
    }
    currPos += headerData.size();//we're now guaranteed to be past the header point, no matter what
  }
  

  void OutProgressiveMP4::setvidTrack(){
    vidTrack = 0;
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
      //Find video track
      if (myMeta.tracks[*it].type == "video"){
        vidTrack = *it;
        break;
      }
    }
  }
  
  //using the fragment number
  //We take the corresponding keyframe and interframes of the main video track
  //and take concurrent frames from its secondary (audio) tracks
  std::map<long unsigned int, fragSet> OutProgressiveMP4::buildFragment(int fragNum){
    std::map<long unsigned int, fragSet> retVal;
    
    long int keyIndex = fragNum + fragKeyNumberShift - (myMeta.tracks[vidTrack].keys.begin()->getNumber() - 1);//here we set the index of the video keyframe we are going to make a fragment of
    
    if (keyIndex < 0 || keyIndex >= myMeta.tracks[vidTrack].keys.size() ){//if the fragnum is not in the keys
      FAIL_MSG("Fragment Number %d not available. KeyShift: %lu FirstKeyNumber: %hu, Calculated KeyIndex: %lu, KeysInMeta: %lu", fragNum, fragKeyNumberShift, myMeta.tracks[vidTrack].keys.begin()->getNumber(), keyIndex, myMeta.tracks[vidTrack].keys.size());
      FAIL_MSG("Current Time: %lu, Current TrackID: %lu", currentPacket.getTime(), currentPacket.getTrackId());
      FAIL_MSG("Rbegin Number: %hu, Rbegin Time %lu, rBegin Length %lu", myMeta.tracks[vidTrack].keys.rbegin()->getNumber(), myMeta.tracks[vidTrack].keys.rbegin()->getTime(), myMeta.tracks[vidTrack].keys.rbegin()->getLength());
      return retVal;
    }
    
    long long int startms = myMeta.tracks[vidTrack].keys[keyIndex].getTime();
    long long int endms;// = startms;
    if (myMeta.tracks[vidTrack].keys.size() > keyIndex + 1){
      endms = myMeta.tracks[vidTrack].keys[keyIndex + 1].getTime();
    }else{
      endms = myMeta.tracks[vidTrack].lastms;
    }
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
      fragSet thisRange;
      thisRange.firstPart = 0;
      thisRange.firstTime = myMeta.tracks[*it].keys.begin()->getTime();
      unsigned long long int prevParts = 0;
      for (std::deque<DTSC::Key>::iterator it2 = myMeta.tracks[*it].keys.begin(); it2 != myMeta.tracks[*it].keys.end(); it2++){
        if (it2->getTime() > startms){
          break;
        }
        thisRange.firstPart += prevParts;
        prevParts = it2->getParts();
        thisRange.firstTime = it2->getTime();
      }
      thisRange.lastPart = thisRange.firstPart;
      thisRange.lastTime = thisRange.firstTime;
      unsigned int curMS = thisRange.firstTime;
      unsigned int nextMS = thisRange.firstTime;
      bool first = true;
      for (int i = thisRange.firstPart; i < myMeta.tracks[*it].parts.size(); i++){
        if (first && curMS >= startms){
          thisRange.firstPart = i;
          thisRange.firstTime = curMS;
          first = false;
        }
        nextMS = curMS + myMeta.tracks[*it].parts[i].getDuration();
        if (nextMS >= endms){
          thisRange.lastPart = i;
          thisRange.lastTime = curMS;
          break;
        }
        curMS = nextMS;
      }
      retVal[*it] = thisRange;
    }
    return retVal;
  }

  unsigned int OutProgressiveMP4::putInFragBuffer(DTSC::Packet& inPack){
    char * dataPointer = 0;
    unsigned int len = 0;
    fragPart temp;
    inPack.getString("data", dataPointer, len);
    temp.data = std::string(dataPointer, len);
    temp.trackID = inPack.getTrackId();
    temp.time = inPack.getTime();
    sortFragBuffer.insert(temp);
    return len;
  }
  
  void OutProgressiveMP4::buildTrafPart(){
    updateMeta();//we need to update meta
    //building set first
    partMap = buildFragment(fragSeqNum);
    if (partMap.size()){
      //generate header
      std::string fragment = fragmentHeader(fragSeqNum, partMap);
      //send content
      myConn.SendNow(fragment);
      fragSeqNum++;
      partListSent = 0;
      //convert map to list here, apologies for inefficiency, but this works best
      //partList = x1 * track y1 + x2 * track y2 * etc.
      partList.clear();
      //std::stringstream temp;
      for (std::map<long unsigned int, fragSet>::iterator it = partMap.begin(); it != partMap.end(); it++){
        partList.resize(partList.size() + (it->second.lastPart - it->second.firstPart) + 1, it->first);
        //temp << "trackID " << it->first << " first: " << it->second.firstPart << " last: " << it->second.lastPart << " tot: " << it->second.lastPart - it->second.firstPart << ";";
      }
    }else{
      DEBUG_MSG(DLVL_WARN, "Warning: partMap should not be empty, but it is!");
      myConn.close();
    }
  }
  
  void OutProgressiveMP4::sendNext(){
    static bool perfect = true;
    char * dataPointer = 0;
    unsigned int len = 0;
    if (myMeta.live){
      //if header needed
      if (partList.empty() || partListSent >= partList.size()){
        buildTrafPart();
      }
      //generate content in mdat, meaning: send right parts
      
      //first check buffer
      while (!sortFragBuffer.empty() && (partListSent < partList.size() && partList[partListSent] == sortFragBuffer.begin()->trackID)){//if packet is needed
        myConn.SendNow(sortFragBuffer.begin()->data);
        partListSent++;
        sortFragBuffer.erase(sortFragBuffer.begin());
        if (partListSent >= partList.size()){
          buildTrafPart();
        }
      }
      
      //then we handle the current packet
      if(partListSent < partList.size() && partList[partListSent] == currentPacket.getTrackId() ){//if packet is needed
        //sending out packet
        currentPacket.getString("data", dataPointer, len);
        myConn.SendNow(dataPointer,len);
        partListSent++;
      }else{
        //Current packet not needed, buffering
        putInFragBuffer(currentPacket);
      }
    }else{//end of fragmenting code
      currentPacket.getString("data", dataPointer, len);
      if ((unsigned long)currentPacket.getTrackId() != sortSet.begin()->trackID || currentPacket.getTime() != sortSet.begin()->time){
        if (perfect){
          DEBUG_MSG(DLVL_WARN, "Warning: input is inconsistent. Expected %lu:%llu but got %ld:%llu", sortSet.begin()->trackID, sortSet.begin()->time, currentPacket.getTrackId(), currentPacket.getTime());
          perfect = false;
        }
      }
      //keep track of where we are
      if (!sortSet.empty()){
        keyPart temp;
        temp.index = sortSet.begin()->index + 1;
        temp.trackID = sortSet.begin()->trackID;
        if(temp.index < myMeta.tracks[temp.trackID].parts.size() ){//only insert when there are parts left
          temp.time = sortSet.begin()->endTime;//timeplace of frame
          temp.endTime = sortSet.begin()->endTime + myMeta.tracks[temp.trackID].parts[temp.index].getDuration();
          temp.size = myMeta.tracks[temp.trackID].parts[temp.index].getSize();//bytesize of frame 
          currPos += sortSet.begin()->size;
          //remove highest keyPart
          sortSet.erase(sortSet.begin());
          sortSet.insert(temp);
        }else{
          currPos += sortSet.begin()->size;
          //remove highest keyPart
          sortSet.erase(sortSet.begin());
        }
      }
      
      
      if (currPos >= byteStart){
        myConn.SendNow(dataPointer, std::min(leftOver, (long long)len));
        //H.Chunkify(Strm.lastData().data(), Strm.lastData().size(), conn);
        leftOver -= len;
      }else{
        if (currPos + (long long)len > byteStart){
          myConn.SendNow(dataPointer+(byteStart-currPos), len-(byteStart-currPos));
          leftOver -= len-(byteStart-currPos);
          currPos = byteStart;
        }
      }
      if (leftOver < 1){
        //stop playback, wait for new request
        stop();
        wantRequest = true;
      }
    }//end if for non fragmented
  }

  void OutProgressiveMP4::sendHeader(){
    seek(seekPoint);
    if (myMeta.live){
      setvidTrack();
    }
    sentHeader = true;
  }
}
