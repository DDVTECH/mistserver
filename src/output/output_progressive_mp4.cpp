#include <mist/defines.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
#include <mist/checksum.h>
#include <mist/bitfields.h>
#include "output_progressive_mp4.h"

#include <inttypes.h>

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
  uint64_t OutProgressiveMP4::estimateFileSize() {
    uint64_t retVal = 0;
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
      for (std::deque<unsigned long>::iterator keyIt = myMeta.tracks[*it].keySizes.begin(); keyIt != myMeta.tracks[*it].keySizes.end(); keyIt++) {
        retVal += *keyIt;
      }
    }
    return retVal * 1.1;
  }

  uint64_t OutProgressiveMP4::mp4HeaderSize(uint64_t & fileSize) {
    bool useLargeBoxes = (estimateFileSize() > 0xFFFFFFFFull);
    uint64_t res = 36 // FTYP Box
      + 8 //MOOV box
      + 108; //MVHD Box
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      DTSC::Track & thisTrack = myMeta.tracks[*it];
      uint64_t tmpRes = 0;
      uint64_t partCount = thisTrack.parts.size();

      tmpRes += 8 //TRAK Box
        + 92 //TKHD Box
        + 36 //EDTS Box
        + 8 //MDIA Box
        + 32 //MDHD Box
        + 33 + thisTrack.getIdentifier().size() // HDLR Box
        + 8 //MINF Box
        + 36 //DINF Box
        + 8; // STBL Box

      //These boxes are empty when generating fragmented output
      tmpRes += 20 + (partCount * 4);//STSZ
      tmpRes += 16 + (partCount * (useLargeBoxes ? 8 : 4));//STCO
      tmpRes += 16 + (1 * 12);//STSC <-- Currently 1 entry, but might become more complex in near future
      
      //Type-specific boxes
      if (thisTrack.type == "video"){
        tmpRes += 20//VMHD Box 
          + 16 //STSD
          + 86 //AVC1
          + 8 + thisTrack.init.size();//avcC
        tmpRes += 16 + (thisTrack.keys.size() * 4);
      }
      if (thisTrack.type == "audio"){
        tmpRes += 16//SMHD Box
          + 16//STSD
          + 36//MP4A
          + 37 + thisTrack.init.size();//ESDS
      }
      
      //Unfortunately, for our STTS and CTTS boxes, we need to loop through all parts of the track
      uint64_t sttsCount = 1;
      uint64_t prevDur = thisTrack.parts[0].getDuration();
      uint64_t prevOffset = thisTrack.parts[0].getOffset();
      uint64_t cttsCount = 1;
      fileSize += thisTrack.parts[0].getSize();
      for (unsigned int part = 1; part < partCount; ++part){
        uint64_t partDur = thisTrack.parts[part].getDuration();
        uint64_t partOffset = thisTrack.parts[part].getOffset();
        uint64_t partSize = thisTrack.parts[part].getSize();
        if (prevDur != partDur){
          prevDur = partDur;
          ++sttsCount;
        }
        if (partOffset != prevOffset){
          prevOffset = partOffset;
          ++cttsCount;
        }
        fileSize += partSize;
      }
      if (cttsCount == 1 && ! prevOffset){
        cttsCount = 0;
      }
      tmpRes += 16 + (sttsCount * 8);//STTS
      if (cttsCount){
        tmpRes += 16 + (cttsCount * 8);//CTTS
      }
      
      res += tmpRes;
    }
    res += 8; //mdat beginning
    fileSize += res;
    return res;
  }


  ///\todo This function does not indicate errors anywhere... maybe fix this...
  std::string OutProgressiveMP4::DTSCMeta2MP4Header(uint64_t & size) {
    //Make sure we have a proper being value for the size...
    size = 0;
    //Stores the result of the function
    std::stringstream header;
    //Determines whether the outputfile is larger than 4GB, in which case we need to use 64-bit boxes for offsets
    bool useLargeBoxes = (estimateFileSize() > 0xFFFFFFFFull);
    //Keeps track of the total size of the mdat box 
    uint64_t mdatSize = 0;


    //Start actually creating the header

    //MP4 Files always start with an FTYP box. Constructor sets default values
    MP4::FTYP ftypBox;
    header.write(ftypBox.asBox(), ftypBox.boxedSize());

    //Start building the moov box. This is the metadata box for an mp4 file, and will contain all metadata. 
    MP4::MOOV moovBox;
    //Keep track of the current index within the moovBox
    unsigned int moovOffset = 0;


    //Construct with duration of -1
    MP4::MVHD mvhdBox(-1);
    //Then override it to set the correct duration
    uint64_t firstms = 0xFFFFFFFFFFFFFFull;
    uint64_t lastms = 0;
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
      lastms = std::max(lastms, (uint64_t)myMeta.tracks[*it].lastms);
      firstms = std::min(firstms, (uint64_t)myMeta.tracks[*it].firstms);
    }
    mvhdBox.setDuration(lastms - firstms);
    //Set the trackid for the first "empty" track within the file.
    mvhdBox.setTrackID(selectedTracks.size() + 1);
    moovBox.setContent(mvhdBox, moovOffset++);

    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
      DTSC::Track & thisTrack = myMeta.tracks[*it];
      size_t partCount = thisTrack.parts.size();
      uint64_t tDuration = thisTrack.lastms - thisTrack.firstms;
      MP4::TRAK trakBox;
      //Keep track of the current index within the moovBox
      unsigned int trakOffset = 0;

      MP4::TKHD tkhdBox(thisTrack, false);
      trakBox.setContent(tkhdBox, trakOffset++);

      //Create an EDTS box, containing an ELST box with default values;
      ///\todo Figure out if this box is really needed for anything.
      MP4::EDTS edtsBox;
      MP4::ELST elstBox;
      elstBox.setVersion(0);
      elstBox.setFlags(0);
      elstBox.setCount(1);
      elstBox.setSegmentDuration(0, tDuration);
      elstBox.setMediaTime(0, 0);
      elstBox.setMediaRateInteger(0, 1);
      elstBox.setMediaRateFraction(0, 0);
      edtsBox.setContent(elstBox, 0);
      trakBox.setContent(edtsBox, trakOffset++);

      MP4::MDIA mdiaBox;
      size_t mdiaOffset = 0;
      
      //Add the mandatory MDHD and HDLR boxes to the MDIA
      MP4::MDHD mdhdBox(tDuration);
      mdhdBox.setLanguage(thisTrack.lang);
      mdiaBox.setContent(mdhdBox, mdiaOffset++);
      MP4::HDLR hdlrBox(thisTrack.type, thisTrack.getIdentifier());
      mdiaBox.setContent(hdlrBox, mdiaOffset++);
      
      MP4::MINF minfBox;
      size_t minfOffset = 0;
      
      //Add a track-type specific box to the MINF box
      if (thisTrack.type == "video") {
        MP4::VMHD vmhdBox;
        vmhdBox.setFlags(1);
        minfBox.setContent(vmhdBox, minfOffset++);
      } else if (thisTrack.type == "audio") {
        MP4::SMHD smhdBox;
        minfBox.setContent(smhdBox, minfOffset++);
      }

      //Add the mandatory DREF (dataReference) box
      MP4::DINF dinfBox;
      MP4::DREF drefBox;
      dinfBox.setContent(drefBox, 0);
      minfBox.setContent(dinfBox, minfOffset++);

     
      MP4::STBL stblBox;
      size_t stblOffset = 0;

      //Add STSD box
      MP4::STSD stsdBox(0);
      if (thisTrack.type == "video") {
        MP4::VisualSampleEntry sampleEntry(thisTrack);
        stsdBox.setEntry(sampleEntry, 0);
      } else if (thisTrack.type == "audio") {
        MP4::AudioSampleEntry sampleEntry(thisTrack);
        stsdBox.setEntry(sampleEntry, 0);
      }
      stblBox.setContent(stsdBox, stblOffset++);

      //Add STTS Box
      MP4::STTS sttsBox(0);
      //Add STSZ Box
      MP4::STSZ stszBox(0);
      MP4::CTTS cttsBox;
      cttsBox.setVersion(0);

      MP4::CTTSEntry tmpEntry;
      tmpEntry.sampleCount = 0;
      tmpEntry.sampleOffset = thisTrack.parts[0].getOffset();
        
      std::deque<std::pair<size_t, size_t> > sttsCounter;
      stszBox.setEntrySize(0, partCount - 1);//Speed up allocation
      size_t totalEntries = 0;

      for (size_t part = 0; part < partCount; ++part){
        stats();
        
        uint64_t partDur = thisTrack.parts[part].getDuration();
        uint64_t partSize = thisTrack.parts[part].getSize();
        uint64_t partOffset = thisTrack.parts[part].getOffset();

        //Create a new entry with current duration if EITHER there is no entry yet, or this parts duration differs from the previous
        if (!sttsCounter.size() || sttsCounter.rbegin()->second != partDur){
          sttsCounter.push_back(std::pair<size_t,size_t>(0, partDur));
        }
        //Update the counter
        sttsCounter.rbegin()->first++;

        stszBox.setEntrySize(partSize, part);
        size += partSize;

        if (partOffset != tmpEntry.sampleOffset) {
          //If the offset of this and previous part differ, write current values and reset
          cttsBox.setCTTSEntry(tmpEntry, totalEntries++);///\todo Again, rewrite for sanity. index FIRST, value SECOND
          tmpEntry.sampleCount = 0;
          tmpEntry.sampleOffset = partOffset;
        }
        tmpEntry.sampleCount++;
      }
        
      MP4::STTSEntry sttsEntry;
      sttsBox.setSTTSEntry(sttsEntry, sttsCounter.size() - 1);
      size_t sttsIdx = 0;
      for (std::deque<std::pair<size_t, size_t> >::iterator it2 = sttsCounter.begin(); it2 != sttsCounter.end(); it2++){
        sttsEntry.sampleCount = it2->first;
        sttsEntry.sampleDelta = it2->second;
        sttsBox.setSTTSEntry(sttsEntry, sttsIdx++);
      }
      if (totalEntries || tmpEntry.sampleOffset) {
        cttsBox.setCTTSEntry(tmpEntry, totalEntries++);
        stblBox.setContent(cttsBox, stblOffset++);
      }
      stblBox.setContent(sttsBox, stblOffset++);
      stblBox.setContent(stszBox, stblOffset++);

      //Add STSS Box IF type is video and we are not fragmented
      if (thisTrack.type == "video") {
        MP4::STSS stssBox(0);
        int tmpCount = 0;
        for (int i = 0; i < thisTrack.keys.size(); i++){
          stssBox.setSampleNumber(tmpCount + 1, i);///\todo PLEASE rewrite this for sanity.... SHOULD be: index FIRST, value SECOND
          tmpCount += thisTrack.keys[i].getParts();
        }
        stblBox.setContent(stssBox, stblOffset++);
      }

      //Add STSC Box
      MP4::STSC stscBox(0);
      MP4::STSCEntry stscEntry(1,1,1);
      stscBox.setSTSCEntry(stscEntry, 0);
      stblBox.setContent(stscBox, stblOffset++);


      //Create STCO Box (either stco or co64)
      //note: Inserting empty values on purpose here, will be fixed later.
      if (useLargeBoxes) {
        MP4::CO64 CO64Box;
        CO64Box.setChunkOffset(0, partCount - 1);
        stblBox.setContent(CO64Box, stblOffset++);
      } else {
        MP4::STCO stcoBox(0);
        stcoBox.setChunkOffset(0, partCount - 1);
        stblBox.setContent(stcoBox, stblOffset++);
      }
      
      minfBox.setContent(stblBox, minfOffset++);
      
      mdiaBox.setContent(minfBox, mdiaOffset++);
      
      trakBox.setContent(mdiaBox, 2);

      moovBox.setContent(trakBox, moovOffset++);
    }
    //initial offset length ftyp, length moov + 8
    uint64_t dataOffset = ftypBox.boxedSize() + moovBox.boxedSize() + 8;

    std::map <size_t, MP4::STCO> checkStcoBoxes;
    std::map <size_t, MP4::CO64> checkCO64Boxes;

    std::deque<MP4::TRAK> trak = moovBox.getChildren<MP4::TRAK>();
    for (std::deque<MP4::TRAK>::iterator trakIt = trak.begin(); trakIt != trak.end(); trakIt++){
      MP4::TKHD tkhdBox = trakIt->getChild<MP4::TKHD>();
      MP4::STBL stblBox = trakIt->getChild<MP4::MDIA>().getChild<MP4::MINF>().getChild<MP4::STBL>();
      if (useLargeBoxes){
        checkCO64Boxes.insert(std::pair<size_t, MP4::CO64>(tkhdBox.getTrackID(), stblBox.getChild<MP4::CO64>()));
      }else{
        checkStcoBoxes.insert(std::pair<size_t, MP4::STCO>(tkhdBox.getTrackID(), stblBox.getChild<MP4::STCO>()));
      }
    }
    //inserting right values in the STCO box header
    //total = 0;
    //Keep track of the current size of the data within the mdat
    uint64_t dataSize = 0;
    //Current values are actual byte offset without header-sized offset
    std::set <keyPart> sortSet;//filling sortset for interleaving parts
    for (std::set<long unsigned int>::iterator subIt = selectedTracks.begin(); subIt != selectedTracks.end(); subIt++) {
      keyPart temp;
      temp.trackID = *subIt;
      temp.time = myMeta.tracks[*subIt].firstms;//timeplace of frame
      temp.index = 0;
      HIGH_MSG("Header sortSet: tid %lu time %lu", temp.trackID, temp.time);
      sortSet.insert(temp);
    }
    while (!sortSet.empty()) {
      stats();
      keyPart temp = *sortSet.begin();
      sortSet.erase(sortSet.begin());
        
      DTSC::Track & thisTrack = myMeta.tracks[temp.trackID];

      //setting the right STCO size in the STCO box
      if (useLargeBoxes){//Re-using the previously defined boolean for speedup
        checkCO64Boxes[temp.trackID].setChunkOffset(dataOffset + dataSize, temp.index);
      } else {
        checkStcoBoxes[temp.trackID].setChunkOffset(dataOffset + dataSize, temp.index);
      }
      dataSize += thisTrack.parts[temp.index].getSize();
      
      //add next keyPart to sortSet
      if (temp.index + 1< thisTrack.parts.size()) {//Only create new element, when there are new elements to be added 
        temp.time += thisTrack.parts[temp.index].getDuration();
        ++temp.index;
        sortSet.insert(temp);
      }
    }

    ///\todo Update this thing for boxes >4G?
    mdatSize = dataSize + 8;//+8 for mp4 header
    
    header << std::string(moovBox.asBox(), moovBox.boxedSize());
    char mdatHeader[8] = {0x00,0x00,0x00,0x00,'m','d','a','t'};
    if (mdatSize < 0xFFFFFFFF){
      Bit::htobl(mdatHeader, mdatSize);
    }
    header << std::string(mdatHeader, 8);
    size += header.str().size();
    return header.str();
  }
  
  /// Calculate a seekPoint, based on byteStart, metadata, tracks and headerSize.
  /// The seekPoint will be set to the timestamp of the first packet to send.
  void OutProgressiveMP4::findSeekPoint(uint64_t byteStart, uint64_t & seekPoint, uint64_t headerSize) {
    seekPoint = 0;
    //if we're starting in the header, seekPoint is always zero.
    if (byteStart <= headerSize) {
      return;
    }
    //okay, we're past the header. Substract the headersize from the starting postion.
    byteStart -= headerSize;
    //forward through the file by headers, until we reach the point where we need to be
    while (!sortSet.empty()) {
      //find the next part and erase it
      keyPart temp = *sortSet.begin();

      DTSC::Track & thisTrack = myMeta.tracks[temp.trackID];
      uint64_t partSize = thisTrack.parts[temp.index].getSize();

      //record where we are
      seekPoint = temp.time;
      //substract the size of this fragment from byteStart
      //if that put us past the point where we wanted to be, return right now
      if (partSize > byteStart) {
        INFO_MSG("We're starting at time %" PRIu64 ", skipping %" PRIu64 " bytes", seekPoint, partSize - byteStart);
        return;
      }
      

      byteStart -= partSize; 

      //otherwise, set currPos to where we are now and continue
      currPos += partSize;

      if (temp.index + 1 < myMeta.tracks[temp.trackID].parts.size()){ //only insert when there are parts left
        temp.time += thisTrack.parts[temp.index].getDuration();
        ++temp.index;
        sortSet.insert(temp);
      }
      //Remove just-parsed element
      sortSet.erase(sortSet.begin());
      //wash, rinse, repeat
    }
    //If we're here, we're in the last fragment.
    //That's technically legal, of course.
  }

  void OutProgressiveMP4::onHTTP() {
    if(H.method == "OPTIONS" || H.method == "HEAD"){
      H.Clean();
      H.setCORSHeaders();
      H.SetHeader("Content-Type", "video/MP4");
      H.SetHeader("Accept-Ranges", "bytes, parsec");
      H.SendResponse("200", "OK", myConn);
      return;
    }

    //Make sure we start receiving data after this function
    ///\todo Should this happen here?
    parseData = true;
    wantRequest = false;
    sentHeader = false;

    fileSize = 0;
    uint64_t headerSize = mp4HeaderSize(fileSize);
    seekPoint = 0;
    byteStart = 0;
    byteEnd = fileSize - 1;
    char rangeType = ' ';
    currPos = 0;
    sortSet.clear();
    for (std::set<long unsigned int>::iterator subIt = selectedTracks.begin(); subIt != selectedTracks.end(); subIt++) {
      keyPart temp;
      temp.trackID = *subIt;
      temp.time = myMeta.tracks[*subIt].firstms;//timeplace of frame
      temp.index = 0;
      sortSet.insert(temp);
    }
    if (H.GetHeader("Range") != ""){
      if (parseRange(byteStart, byteEnd)){
        findSeekPoint(byteStart, seekPoint, headerSize);
      }
      rangeType = H.GetHeader("Range")[0];
    }
    H.Clean(); //make sure no parts of old requests are left in any buffers
    H.setCORSHeaders();
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
        H.SetHeader("Content-Range", rangeReply.str());
        /// \todo Switch to chunked?
        H.SendResponse("206", "Partial content", myConn);
        //H.StartResponse("206", "Partial content", HTTP_R, conn);
      }
    }else{
      H.SetHeader("Content-Length", byteEnd - byteStart + 1);
      /// \todo Switch to chunked?
      H.SendResponse("200", "OK", myConn);
      //HTTP_S.StartResponse(HTTP_R, conn);
    }
    leftOver = byteEnd - byteStart + 1;//add one byte, because range "0-0" = 1 byte of data
    if (byteStart < headerSize) {
      std::string headerData = DTSCMeta2MP4Header(fileSize);
      myConn.SendNow(headerData.data() + byteStart, std::min(headerSize, byteEnd) - byteStart); //send MP4 header
      leftOver -= std::min(headerSize, byteEnd) - byteStart;
    }
    currPos += headerSize;//we're now guaranteed to be past the header point, no matter what
  }
  
  void OutProgressiveMP4::sendNext() {
    static bool perfect = true;
    
    //Obtain a pointer to the data of this packet
    char * dataPointer = 0;
    unsigned int len = 0;
    thisPacket.getString("data", dataPointer, len);

    keyPart thisPart = *sortSet.begin();
    uint64_t thisSize = myMeta.tracks[thisPart.trackID].parts[thisPart.index].getSize();
    if ((unsigned long)thisPacket.getTrackId() != thisPart.trackID || thisPacket.getTime() != thisPart.time || len != thisSize){
      if (thisPacket.getTime() > sortSet.begin()->time || thisPacket.getTrackId() > sortSet.begin()->trackID) {
        if (perfect) {
          WARN_MSG("Warning: input is inconsistent. Expected %lu:%lu but got %ld:%llu - cancelling playback", thisPart.trackID, thisPart.time, thisPacket.getTrackId(), thisPacket.getTime());
          perfect = false;
          myConn.close();
        }
      } else {
        WARN_MSG("Did not receive expected %lu:%lu (%lub) but got %ld:%llu (%ub) - throwing it away", thisPart.trackID, thisPart.time, thisSize, thisPacket.getTrackId(), thisPacket.getTime(), len);
      }
      return;
    }

    if (currPos >= byteStart) {
      myConn.SendNow(dataPointer, std::min(leftOver, (int64_t)len));
      leftOver -= len;
    } else {
      if (currPos + (long long)len > byteStart) {
        myConn.SendNow(dataPointer + (byteStart - currPos), std::min(leftOver, (int64_t)(len - (byteStart - currPos))));
        leftOver -= len - (byteStart - currPos);
      }
    }

    //keep track of where we are
    if (!sortSet.empty()) {
      keyPart temp = *sortSet.begin();
      sortSet.erase(sortSet.begin());



      DTSC::Track & thisTrack = myMeta.tracks[temp.trackID];

      currPos += thisTrack.parts[temp.index].getSize();
      if (temp.index + 1 < thisTrack.parts.size()) { //only insert when there are parts left
        temp.time += thisTrack.parts[temp.index].getDuration();
        ++temp.index;
        sortSet.insert(temp);
      }

    }

    if (leftOver < 1) {
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
