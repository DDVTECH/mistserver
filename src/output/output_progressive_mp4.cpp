#include "output_progressive_mp4.h"
#include <mist/defines.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
#include <mist/checksum.h>

namespace Mist {
  OutProgressiveMP4::OutProgressiveMP4(Socket::Connection & conn) : HTTPOutput(conn){}
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
  
  std::string OutProgressiveMP4::DTSCMeta2MP4Header(long long & size){
    std::stringstream header;
    //ftyp box
    MP4::FTYP ftypBox;
    header << std::string(ftypBox.asBox(),ftypBox.boxedSize());
    
    uint64_t mdatSize = 0;
    //moov box
    MP4::MOOV moovBox;
    unsigned int moovOffset = 0;
    {
      //calculating longest duration
      long long int firstms = -1;
      long long int lastms = -1;
      for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
        if (lastms == -1 || lastms < (long long)myMeta.tracks[*it].lastms){
          lastms = myMeta.tracks[*it].lastms;
        }
        if (firstms == -1 || firstms > (long long)myMeta.tracks[*it].firstms){
          firstms = myMeta.tracks[*it].firstms;
        }
      }
      MP4::MVHD mvhdBox(lastms - firstms);
      moovBox.setContent(mvhdBox, moovOffset++);
    }
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
      MP4::TRAK trakBox;
      {
        {
          MP4::TKHD tkhdBox(*it, myMeta.tracks[*it].lastms - myMeta.tracks[*it].firstms, myMeta.tracks[*it].width, myMeta.tracks[*it].height);
          trakBox.setContent(tkhdBox, 0);
        }{
          MP4::MDIA mdiaBox;
          unsigned int mdiaOffset = 0;
          {
            MP4::MDHD mdhdBox(myMeta.tracks[*it].lastms - myMeta.tracks[*it].firstms);
            mdiaBox.setContent(mdhdBox, mdiaOffset++);
          }//MDHD box
          {
            MP4::HDLR hdlrBox(myMeta.tracks[*it].type, myMeta.tracks[*it].getIdentifier());
            mdiaBox.setContent(hdlrBox, mdiaOffset++);
          }//hdlr box
          {
            MP4::MINF minfBox;
            unsigned int minfOffset = 0;
            if (myMeta.tracks[*it].type== "video"){
              MP4::VMHD vmhdBox;
              vmhdBox.setFlags(1);
              minfBox.setContent(vmhdBox,minfOffset++);
            }else if (myMeta.tracks[*it].type == "audio"){
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
              MP4::STBL stblBox;
              unsigned int offset = 0;
              {
                MP4::STSD stsdBox;
                stsdBox.setVersion(0);
                if (myMeta.tracks[*it].type == "video"){//boxname = codec
                  MP4::VisualSampleEntry vse;
                  if (myMeta.tracks[*it].codec == "H264"){
                    vse.setCodec("avc1");
                  }
                  if (myMeta.tracks[*it].codec == "HEVC"){
                    vse.setCodec("hev1");
                  }
                  vse.setDataReferenceIndex(1);
                  vse.setWidth(myMeta.tracks[*it].width);
                  vse.setHeight(myMeta.tracks[*it].height);
                  if (myMeta.tracks[*it].codec == "H264"){
                    MP4::AVCC avccBox;
                    avccBox.setPayload(myMeta.tracks[*it].init);
                    vse.setCLAP(avccBox);
                  }
                  if (myMeta.tracks[*it].codec == "HEVC"){
                    MP4::HVCC hvccBox;
                    hvccBox.setPayload(myMeta.tracks[*it].init);
                    vse.setCLAP(hvccBox);
                  }
                  stsdBox.setEntry(vse,0);
                }else if(myMeta.tracks[*it].type == "audio"){//boxname = codec
                  MP4::AudioSampleEntry ase;
                  if (myMeta.tracks[*it].codec == "AAC"){
                    ase.setCodec("mp4a");
                    ase.setDataReferenceIndex(1);
                  }else if (myMeta.tracks[*it].codec == "MP3"){
                    ase.setCodec("mp4a");
                    ase.setDataReferenceIndex(1);
                  }
                  ase.setSampleRate(myMeta.tracks[*it].rate);
                  ase.setChannelCount(myMeta.tracks[*it].channels);
                  ase.setSampleSize(myMeta.tracks[*it].size);
                  //MP4::ESDS esdsBox(myMeta.tracks[*it].init, myMeta.tracks[*it].bps);
                  MP4::ESDS esdsBox;
                  
                  //outputting these values first, so malloc isn't called as often.
                  if (myMeta.tracks[*it].codec == "MP3"){
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
                    esdsBox.setESHeaderStartCodes(myMeta.tracks[*it].init);
                    esdsBox.setConfigDescriptorTypeLength(myMeta.tracks[*it].init.size());
                    esdsBox.setSLConfigExtendedDescriptorTypeTag(0x808080);
                    esdsBox.setSLDescriptorTypeLength(1);
                    esdsBox.setESDescriptorTypeLength(32+myMeta.tracks[*it].init.size());
                    esdsBox.setSLConfigDescriptorTypeTag(0x6);
                    esdsBox.setSLValue(2);
                    esdsBox.setDecoderConfigDescriptorTypeLength(18 + myMeta.tracks[*it].init.size());
                    esdsBox.setByteObjectTypeID(0x40);
                  }
                  esdsBox.setESID(2);
                  esdsBox.setStreamPriority(0);
                  esdsBox.setStreamType(5);
                  esdsBox.setReservedFlag(1);
                  esdsBox.setMaximumBitRate(10000000);
                  esdsBox.setAverageBitRate(myMeta.tracks[*it].bps * 8);
                  esdsBox.setBufferSize(1250000);
                  ase.setCodecBox(esdsBox);
                  stsdBox.setEntry(ase,0);
                }
                stblBox.setContent(stsdBox,offset++);
              }//stsd box
              {
                MP4::STTS sttsBox;
                sttsBox.setVersion(0);
                if (myMeta.tracks[*it].parts.size()){
                  for (unsigned int part = 0; part < myMeta.tracks[*it].parts.size(); part++){
                    MP4::STTSEntry newEntry;
                    newEntry.sampleCount = 1;
                    newEntry.sampleDelta = myMeta.tracks[*it].parts[part].getDuration();
                    sttsBox.setSTTSEntry(newEntry, part);
                  }
                }
                stblBox.setContent(sttsBox,offset++);
              }//stts box
              if (myMeta.tracks[*it].type == "video"){
                //STSS Box here
                MP4::STSS stssBox;
                stssBox.setVersion(0);
                int tmpCount = 0;
                int tmpItCount = 0;
                for ( std::deque< DTSC::Key>::iterator tmpIt = myMeta.tracks[*it].keys.begin(); tmpIt != myMeta.tracks[*it].keys.end(); tmpIt ++) {
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
                for (std::deque< DTSC::Part>::iterator partIt = myMeta.tracks[*it].parts.begin(); partIt != myMeta.tracks[*it].parts.end(); partIt ++) {
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
                if (myMeta.tracks[*it].parts.size() != 0){
                  stcoBox.setChunkOffset(0, myMeta.tracks[*it].parts.size() - 1);//this inserts all empty entries at once
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
    }
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
    std::set <keyPart> sortSet;//filling sortset for interleaving parts
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
      //setting the right STCO size in the STCO box
      checkStcoBoxes[sortSet.begin()->trackID].setChunkOffset(totalByteOffset + byteOffset, sortSet.begin()->index);
      totalByteOffset += sortSet.begin()->size;
      //add keyPart to sortSet
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
    }

    mdatSize = totalByteOffset+8;
    
    header << std::string(moovBox.asBox(),moovBox.boxedSize());
    
    header << (char)((mdatSize>>24) & 0xFF) << (char)((mdatSize>>16) & 0xFF) << (char)((mdatSize>>8) & 0xFF) << (char)(mdatSize & 0xFF) << "mdat";
    //end of header
    
    size += header.str().size();
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
    std::string headerData = DTSCMeta2MP4Header(fileSize);
    byteStart = 0;
    byteEnd = fileSize - 1;
    seekPoint = 0;
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
    if (H.GetHeader("Range") != ""){
      parseRange(H.GetHeader("Range"), byteStart, byteEnd, seekPoint, headerData.size());
      rangeType = H.GetHeader("Range")[0];
    }
    H.Clean(); //make sure no parts of old requests are left in any buffers
    H.SetHeader("Content-Type", "video/MP4"); //Send the correct content-type for MP4 files
    H.SetHeader("Accept-Ranges", "bytes, parsec");
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
        //H.StartResponse("206", "Partial content", HTTP_R, conn);
      }
    }else{
      H.SetHeader("Content-Length", byteEnd - byteStart + 1);
      //do not multiplex requests that aren't ranged
      H.SetHeader("MistMultiplex", "No");
      /// \todo Switch to chunked?
      H.SendResponse("200", "OK", myConn);
      //HTTP_S.StartResponse(HTTP_R, conn);
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
  
  void OutProgressiveMP4::sendNext(){
    static bool perfect = true;
    char * dataPointer = 0;
    unsigned int len = 0;
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
    //sortSet.clear();//we don't need you anymore!
    if (leftOver < 1){
      //stop playback, wait for new request
      stop();
      wantRequest = true;
    }
  }

  void OutProgressiveMP4::sendHeader(){
    seek(seekPoint);
    sentHeader = true;
  }
  
}
