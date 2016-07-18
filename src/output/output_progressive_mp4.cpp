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
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/mp4";
    capa["methods"][0u]["priority"] = 8ll;
    capa["methods"][0u]["nolive"] = 1;
  }
  
  long long unsigned OutProgressiveMP4::estimateFileSize(){
    long long unsigned retVal = 0;
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      for (std::deque<unsigned long>::iterator keyIt = myMeta.tracks[*it].keySizes.begin(); keyIt != myMeta.tracks[*it].keySizes.end(); keyIt++){
        retVal += *keyIt;
      }
    }
    return retVal * 1.1;
  }
  
  std::string OutProgressiveMP4::DTSCMeta2MP4Header(long long & size){
    std::stringstream header;
    //ftyp box
    MP4::FTYP ftypBox;
    header.write(ftypBox.asBox(),ftypBox.boxedSize());
    bool biggerThan4G = (estimateFileSize() > 0xFFFFFFFFull);
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
      DTSC::Track & thisTrack = myMeta.tracks[*it];
      MP4::TRAK trakBox;
      {
        {
          MP4::TKHD tkhdBox(*it, thisTrack.lastms - thisTrack.firstms, thisTrack.width, thisTrack.height);
          trakBox.setContent(tkhdBox, 0);
        }{
          MP4::MDIA mdiaBox;
          unsigned int mdiaOffset = 0;
          {
            MP4::MDHD mdhdBox(thisTrack.lastms - thisTrack.firstms);
            mdhdBox.setLanguage(thisTrack.lang);
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
              MP4::STBL stblBox;
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
                  MP4::ESDS esdsBox(thisTrack.init);
                  ase.setCodecBox(esdsBox);
                  stsdBox.setEntry(ase,0);
                }
                stblBox.setContent(stsdBox,offset++);
              }//stsd box
              {
                MP4::STTS sttsBox;
                sttsBox.setVersion(0);
                if (thisTrack.parts.size()){
                  for (unsigned int part = thisTrack.parts.size(); part > 0; --part){
                    MP4::STTSEntry newEntry;
                    newEntry.sampleCount = 1;
                    newEntry.sampleDelta = thisTrack.parts[part-1].getDuration();
                    sttsBox.setSTTSEntry(newEntry, part-1);
                  }
                }
                stblBox.setContent(sttsBox,offset++);
              }//stts box
              if (thisTrack.type == "video"){
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
                MP4::STSCEntry stscEntry;
                stscEntry.firstChunk = 1;
                stscEntry.samplesPerChunk = 1;
                stscEntry.sampleDescriptionIndex = 1;
                stscBox.setSTSCEntry(stscEntry, 0);
                stblBox.setContent(stscBox,offset++);
              }//stsc box
              {
                bool makeCTTS = false;
                MP4::STSZ stszBox;
                stszBox.setVersion(0);
                if (thisTrack.parts.size()){
                  std::deque<DTSC::Part>::reverse_iterator tmpIt = thisTrack.parts.rbegin();
                  for (unsigned int part = thisTrack.parts.size(); part > 0; --part){
                    unsigned int partSize = tmpIt->getSize();
                    stszBox.setEntrySize(partSize, part-1);//in bytes in file
                    size += partSize;
                    makeCTTS |= tmpIt->getOffset();
                    tmpIt++;
                  }
                }
                if (makeCTTS){
                  MP4::CTTS cttsBox;
                  cttsBox.setVersion(0);
                  if (thisTrack.parts.size()){
                    std::deque<DTSC::Part>::iterator tmpIt = thisTrack.parts.begin();
                    MP4::CTTSEntry tmpEntry;
                    tmpEntry.sampleCount = 1;
                    tmpEntry.sampleOffset = tmpIt->getOffset();
                    unsigned int totalEntries = 0;
                    tmpIt++;
                    while (tmpIt != thisTrack.parts.end()){
                      unsigned int timeOffset = tmpIt->getOffset();
                      if (timeOffset == tmpEntry.sampleOffset){
                        tmpEntry.sampleCount++;
                      }else{
                        cttsBox.setCTTSEntry(tmpEntry, totalEntries++);
                        tmpEntry.sampleCount = 1;
                        tmpEntry.sampleOffset = timeOffset;
                      }
                      tmpIt++;
                    }
                    cttsBox.setCTTSEntry(tmpEntry, totalEntries++);
                    //cttsBox.setEntryCount(totalEntries);
                  }
                  stblBox.setContent(cttsBox,offset++);
                }//ctts
                stblBox.setContent(stszBox,offset++);
              }//stsz box
              {
                if (biggerThan4G){
                  MP4::CO64 CO64Box;
                  //Inserting empty values on purpose here, will be fixed later.
                  if (thisTrack.parts.size() != 0){
                    CO64Box.setChunkOffset(0, thisTrack.parts.size() - 1);//this inserts all empty entries at once
                  }
                  stblBox.setContent(CO64Box,offset++);
                }else{
                  MP4::STCO stcoBox;
                  //Inserting empty values on purpose here, will be fixed later.
                  if (thisTrack.parts.size() != 0){
                    stcoBox.setChunkOffset(0, thisTrack.parts.size() - 1);//this inserts all empty entries at once
                  }
                  stblBox.setContent(stcoBox,offset++);
                }
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
    //initial offset length ftyp, length moov + 8
    unsigned long long int byteOffset = ftypBox.boxedSize() + moovBox.boxedSize() + 8;
    //update all STCO or CO64 from the following maps;
    std::map <long unsigned, MP4::STCO> checkStcoBoxes;
    std::map <long unsigned, MP4::CO64> checkCO64Boxes;
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
          checkStcoBoxes.insert( std::pair<long unsigned, MP4::STCO>(((MP4::TKHD&)checkTkhdBox).getTrackID(), ((MP4::STCO&)checkStblBox.getContent(j)) ));
          break;
        }
        if (checkStblBox.getContent(j).isType("co64")){
          checkCO64Boxes.insert( std::pair<long unsigned, MP4::CO64>(((MP4::TKHD&)checkTkhdBox).getTrackID(), ((MP4::CO64&)checkStblBox.getContent(j)) ));
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
      std::set<keyPart>::iterator keyBegin = sortSet.begin();
      //setting the right STCO size in the STCO box
      if (checkCO64Boxes.count(keyBegin->trackID)){
        checkCO64Boxes[keyBegin->trackID].setChunkOffset(totalByteOffset + byteOffset, keyBegin->index);
      }else{
        checkStcoBoxes[keyBegin->trackID].setChunkOffset(totalByteOffset + byteOffset, keyBegin->index);
      }
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
    
    header.write(moovBox.asBox(),moovBox.boxedSize());
    
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
        if (byteEnd > size - 1){byteEnd = size - 1;}
      }else{
        byteEnd = size;
      }
      DEBUG_MSG(DLVL_MEDIUM, "Range request: %lli-%lli (%s)", byteStart, byteEnd, header.c_str());
      findSeekPoint(byteStart, seekPoint, headerSize);
      return;
    }
  }
  void OutProgressiveMP4::onHTTP(){
    if(H.method == "OPTIONS" || H.method == "HEAD"){
      H.Clean();
      H.setCORSHeaders();
      H.SetHeader("Content-Type", "video/MP4");
      H.SetHeader("Accept-Ranges", "bytes, parsec");
      H.SendResponse("200", "OK", myConn);
      return;
    }

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
    thisPacket.getString("data", dataPointer, len);
    if ((unsigned long)thisPacket.getTrackId() != sortSet.begin()->trackID || thisPacket.getTime() != sortSet.begin()->time){
      if (thisPacket.getTime() >= sortSet.begin()->time || (unsigned long)thisPacket.getTrackId() >= sortSet.begin()->trackID){
        if (perfect){
          DEBUG_MSG(DLVL_WARN, "Warning: input is inconsistent. Expected %lu:%llu but got %ld:%llu - cancelling playback", sortSet.begin()->trackID, sortSet.begin()->time, thisPacket.getTrackId(), thisPacket.getTime());
          perfect = false;
          myConn.close();
        }
      }else{
        DEBUG_MSG(DLVL_HIGH, "Did not receive expected %lu:%llu but got %ld:%llu - throwing it away", sortSet.begin()->trackID, sortSet.begin()->time, thisPacket.getTrackId(), thisPacket.getTime());
      }
      return;
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
        sortSet.insert(temp);
      }
      currPos += sortSet.begin()->size;
      //remove highest keyPart
      sortSet.erase(sortSet.begin());
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
