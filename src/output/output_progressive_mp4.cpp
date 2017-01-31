#include <mist/defines.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
#include <mist/checksum.h>
#include <mist/bitfields.h>
#include "output_progressive_mp4.h"

namespace Mist {
  OutProgressiveMP4::OutProgressiveMP4(Socket::Connection & conn) : HTTPOutput(conn) {}
  OutProgressiveMP4::~OutProgressiveMP4() {}

  void OutProgressiveMP4::init(Util::Config * cfg) {
    HTTPOutput::init(cfg);
    capa["name"] = "MP4";
    capa["desc"] = "Enables HTTP protocol progressive streaming.";
    capa["url_rel"] = "/$.mp4";
    capa["url_match"][0u] = "/$.mp4";
    capa["url_match"][1u] = "/$.3gp";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("AC3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/mp4";
    capa["methods"][0u]["priority"] = 8ll;
    ///\todo uncomment when we actually start implementing mp4 recording
    //capa["canRecord"].append("mp4");
    //capa["canRecord"].append("m3u");
  }

  long long unsigned OutProgressiveMP4::estimateFileSize() {
    long long unsigned retVal = 0;
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
      for (std::deque<unsigned long>::iterator keyIt = myMeta.tracks[*it].keySizes.begin(); keyIt != myMeta.tracks[*it].keySizes.end(); keyIt++) {
        retVal += *keyIt;
      }
    }
    return retVal * 1.1;
  }

  ///\todo This function does not indicate errors anywhere... maybe fix this...
  std::string OutProgressiveMP4::DTSCMeta2MP4Header(long long & size, int fragmented) {
    if (myMeta.live){
      needsLookAhead = 420;
    }
    //Make sure we have a proper being value for the size...
    size = 0;
    //Stores the result of the function
    std::stringstream header;
    //Determines whether the outputfile is larger than 4GB, in which case we need to use 64-bit boxes for offsets
    bool useLargeBoxes = !fragmented && (estimateFileSize() > 0xFFFFFFFFull);
    //Keeps track of the total size of the mdat box 
    uint64_t mdatSize = 0;


    //Start actually creating the header

    //MP4 Files always start with an FTYP box. Constructor sets default values
    MP4::FTYP ftypBox;
    if (sending3GP) {
      ftypBox.setMajorBrand("3gp6");
      ftypBox.setCompatibleBrands("3gp6", 3);
    }
    header.write(ftypBox.asBox(), ftypBox.boxedSize());

    //Start building the moov box. This is the metadata box for an mp4 file, and will contain all metadata. 
    MP4::MOOV moovBox;
    //Keep track of the current index within the moovBox
    unsigned int moovOffset = 0;


    //Construct with duration of -1, as this is the default for fragmented
    MP4::MVHD mvhdBox(-1);
    //Then override it only when we are not sending a fragmented file
    if (!fragmented){
      //calculating longest duration
      long long unsigned firstms = 0xFFFFFFFFFFFFFFull;
      long long unsigned lastms = 0;
      for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
        lastms = std::max(lastms, myMeta.tracks[*it].lastms);
        firstms = std::min(firstms, myMeta.tracks[*it].firstms);
      }
      mvhdBox.setDuration(lastms - firstms);
    }
    //Set the trackid for the first "empty" track within the file.
    mvhdBox.setTrackID(selectedTracks.size() + 1);
    moovBox.setContent(mvhdBox, moovOffset++);

    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
      DTSC::Track & thisTrack = myMeta.tracks[*it];
      MP4::TRAK trakBox;
      //Keep track of the current index within the moovBox
      unsigned int trakOffset = 0;

      MP4::TKHD tkhdBox(thisTrack, fragmented);
      if (fragmented){
        tkhdBox.setDuration(-1);
      }
      trakBox.setContent(tkhdBox, trakOffset++);

      //Create an EDTS box, containing an ELST box with default values;
      ///\todo Figure out if this box is really needed for anything.
      MP4::EDTS edtsBox;
      MP4::ELST elstBox;
      elstBox.setVersion(0);
      elstBox.setFlags(0);
      elstBox.setCount(1);
      if (!fragmented){
        elstBox.setSegmentDuration(0, thisTrack.lastms - thisTrack.firstms);
      }else{
        elstBox.setSegmentDuration(0, -1);
      }
      elstBox.setMediaTime(0, 0);
      elstBox.setMediaRateInteger(0, 1);
      elstBox.setMediaRateFraction(0, 0);
      edtsBox.setContent(elstBox, 0);
      trakBox.setContent(edtsBox, trakOffset++);

      MP4::MDIA mdiaBox;
      unsigned int mdiaOffset = 0;
      
      //Add the mandatory MDHD and HDLR boxes to the MDIA
      MP4::MDHD mdhdBox(thisTrack.lastms - thisTrack.firstms);
      if (fragmented){
        mdhdBox.setDuration(-1);
      }
      mdhdBox.setLanguage(thisTrack.lang);
      mdiaBox.setContent(mdhdBox, mdiaOffset++);
      MP4::HDLR hdlrBox(thisTrack.type, thisTrack.getIdentifier());
      mdiaBox.setContent(hdlrBox, mdiaOffset++);
      
      MP4::MINF minfBox;
      unsigned int minfOffset = 0;
      
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
      unsigned int stblOffset = 0;

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
      //note: STTS is empty when fragmented
      MP4::STTS sttsBox(0);
      if (!fragmented) {
        std::deque<std::pair<int, int> > sttsCounter;
        for (unsigned int part = 0; part < thisTrack.parts.size(); ++part) {
          //Create a new entry with current duration if EITHER there is no entry yet, or this parts duration differs from the previous
          if (!sttsCounter.size() || sttsCounter.rbegin()->second != thisTrack.parts[part].getDuration()){
            //Set the counter to 0, so we don't have to handle this situation diffent when updating
            sttsCounter.push_back(std::pair<int,int>(0, thisTrack.parts[part].getDuration()));
          }
          //Then update the counter
          sttsCounter.rbegin()->first++;
        }

        //Write all entries in reverse
        for (unsigned int entry = sttsCounter.size(); entry > 0; --entry){
          MP4::STTSEntry newEntry;
          newEntry.sampleCount = sttsCounter[entry - 1].first;;
          newEntry.sampleDelta = sttsCounter[entry - 1].second;
          sttsBox.setSTTSEntry(newEntry, entry - 1);///\todo rewrite for sanity
        }
      }
      stblBox.setContent(sttsBox, stblOffset++);

      //Add STSS Box IF type is video and we are not fragmented
      if (thisTrack.type == "video" && !fragmented) {
        MP4::STSS stssBox(0);
        int tmpCount = 0;
        for (int i = 0; i < thisTrack.keys.size(); i++){
          stssBox.setSampleNumber(tmpCount + 1, i);///\todo PLEASE rewrite this for sanity.... SHOULD be: index FIRST, value SECOND
          tmpCount += thisTrack.keys[i].getParts();
        }
        stblBox.setContent(stssBox, stblOffset++);
      }

      //Add STSC Box
      //note: STSC is empty when fragmented
      MP4::STSC stscBox(0);
      if (!fragmented) {
        MP4::STSCEntry stscEntry(1,1,1);
        stscBox.setSTSCEntry(stscEntry, 0);
      }
      stblBox.setContent(stscBox, stblOffset++);

      bool containsOffsets = false;

      //Add STSZ Box
      //note: STSZ is empty when fragmented
      MP4::STSZ stszBox(0);
      if (!fragmented) {
        if (thisTrack.parts.size()) {
          std::deque<DTSC::Part>::reverse_iterator tmpIt = thisTrack.parts.rbegin();
          for (unsigned int part = thisTrack.parts.size(); part > 0; --part) {
            ///\todo rewrite for sanity
            stszBox.setEntrySize(tmpIt->getSize(), part - 1); //in bytes in file
            size += tmpIt->getSize();
            containsOffsets |= tmpIt->getOffset();
            tmpIt++;
          }
        }
      }
      stblBox.setContent(stszBox, stblOffset++);
      
      //Add CTTS Box only if the track contains time offsets
      //note: CTTS will never exist in fragmented, since containsOffsets is set while generating the STSZ box
      if (containsOffsets) {
        MP4::CTTS cttsBox;
        cttsBox.setVersion(0);

        MP4::CTTSEntry tmpEntry;
        tmpEntry.sampleCount = 0;
        tmpEntry.sampleOffset = thisTrack.parts[0].getOffset();
        unsigned int totalEntries = 0;
        for (std::deque<DTSC::Part>::iterator tmpIt = thisTrack.parts.begin(); tmpIt != thisTrack.parts.end(); tmpIt++){
          if (tmpIt->getOffset() != tmpEntry.sampleOffset) {
            //If the offset of this and previous part differ, write current values and reset
            cttsBox.setCTTSEntry(tmpEntry, totalEntries++);///\todo Again, rewrite for sanity. index FIRST, value SECOND
            tmpEntry.sampleCount = 0;
            tmpEntry.sampleOffset = tmpIt->getOffset();
          }
          tmpEntry.sampleCount++;
        }
        //set the last entry
        cttsBox.setCTTSEntry(tmpEntry, totalEntries++);
        stblBox.setContent(cttsBox, stblOffset++);
      }

     
      //Create STCO Box (either stco or co64)
      //note: 64bit boxes will never be used in fragmented
      //note: Inserting empty values on purpose here, will be fixed later.
      if (useLargeBoxes) {
        MP4::CO64 CO64Box;
        CO64Box.setChunkOffset(0, thisTrack.parts.size() - 1);
        stblBox.setContent(CO64Box, stblOffset++);
      } else {
        MP4::STCO stcoBox(0);
        if (fragmented) {
          stcoBox.setEntryCount(0);
        } else {
          stcoBox.setChunkOffset(0, thisTrack.parts.size() - 1);
        }
        stblBox.setContent(stcoBox, stblOffset++);
      }
      
      minfBox.setContent(stblBox, minfOffset++);
      
      mdiaBox.setContent(minfBox, mdiaOffset++);
      
      trakBox.setContent(mdiaBox, 2);

      moovBox.setContent(trakBox, moovOffset++);
    }

    if (fragmented) {
      MP4::MVEX mvexBox;
      unsigned int curBox = 0;
      for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
        MP4::TREX trexBox(*it);
        mvexBox.setContent(trexBox, curBox++);
      }
      moovBox.setContent(mvexBox, moovOffset++);
    }else{ //if we are making a non fragmented MP4 and there are parts
      //initial offset length ftyp, length moov + 8
      unsigned long long int dataOffset = ftypBox.boxedSize() + moovBox.boxedSize() + 8;
      //update all STCO or CO64 from the following maps;
      std::map <long unsigned, MP4::STCO> checkStcoBoxes;
      std::map <long unsigned, MP4::CO64> checkCO64Boxes;
      //for all tracks
      for (unsigned int i = 1; i < moovBox.getContentCount(); i++) {
        //10 lines to get the STCO box.
        MP4::TRAK checkTrakBox;
        MP4::Box checkMdiaBox;
        MP4::Box checkTkhdBox;
        MP4::MINF checkMinfBox;
        MP4::STBL checkStblBox;
        //MP4::STCO checkStcoBox;
        checkTrakBox = ((MP4::TRAK &)moovBox.getContent(i));
        for (unsigned int j = 0; j < checkTrakBox.getContentCount(); j++) {
          if (checkTrakBox.getContent(j).isType("mdia")) {
            checkMdiaBox = checkTrakBox.getContent(j);
            break;
          }
          if (checkTrakBox.getContent(j).isType("tkhd")) {
            checkTkhdBox = checkTrakBox.getContent(j);
          }
        }
        for (unsigned int j = 0; j < ((MP4::MDIA &)checkMdiaBox).getContentCount(); j++) {
          if (((MP4::MDIA &)checkMdiaBox).getContent(j).isType("minf")) {
            checkMinfBox = ((MP4::MINF &)((MP4::MDIA &)checkMdiaBox).getContent(j));
            break;
          }
        }
        for (unsigned int j = 0; j < checkMinfBox.getContentCount(); j++) {
          if (checkMinfBox.getContent(j).isType("stbl")) {
            checkStblBox = ((MP4::STBL &)checkMinfBox.getContent(j));
            break;
          }
        }
        for (unsigned int j = 0; j < checkStblBox.getContentCount(); j++) {
          if (checkStblBox.getContent(j).isType("stco")) {
            checkStcoBoxes.insert(std::pair<long unsigned, MP4::STCO>(((MP4::TKHD &)checkTkhdBox).getTrackID(), ((MP4::STCO &)checkStblBox.getContent(j))));
            break;
          }
          if (checkStblBox.getContent(j).isType("co64")) {
            checkCO64Boxes.insert(std::pair<long unsigned, MP4::CO64>(((MP4::TKHD &)checkTkhdBox).getTrackID(), ((MP4::CO64 &)checkStblBox.getContent(j))));
            break;
          }
        }
      }
      //inserting right values in the STCO box header
      //total = 0;
      //Keep track of the current size of the data within the mdat
      long long unsigned int dataSize = 0;
      //Current values are actual byte offset without header-sized offset
      std::set <keyPart> sortSet;//filling sortset for interleaving parts
      for (std::set<long unsigned int>::iterator subIt = selectedTracks.begin(); subIt != selectedTracks.end(); subIt++) {
        keyPart temp;
        temp.trackID = *subIt;
        temp.time = myMeta.tracks[*subIt].firstms;//timeplace of frame
        temp.endTime = myMeta.tracks[*subIt].firstms + myMeta.tracks[*subIt].parts[0].getDuration();
        temp.size = myMeta.tracks[*subIt].parts[0].getSize();//bytesize of frame (alle parts all together)
        temp.index = 0;
        INFO_MSG("adding to sortSet: tid %lu time %llu", temp.trackID, temp.time);
        sortSet.insert(temp);
      }
      while (!sortSet.empty()) {
        std::set<keyPart>::iterator keyBegin = sortSet.begin();
        //setting the right STCO size in the STCO box
        if (useLargeBoxes){//Re-using the previously defined boolean for speedup
          checkCO64Boxes[keyBegin->trackID].setChunkOffset(dataOffset + dataSize, keyBegin->index);
        } else {
          checkStcoBoxes[keyBegin->trackID].setChunkOffset(dataOffset + dataSize, keyBegin->index);
        }
        dataSize += keyBegin->size;
        
        //add next keyPart to sortSet
        DTSC::Track & thisTrack = myMeta.tracks[keyBegin->trackID];
        if (keyBegin->index < thisTrack.parts.size() - 1) {//Only create new element, when there are new elements to be added 
          keyPart temp = *keyBegin;
          temp.index ++;
          temp.time = temp.endTime;
          temp.endTime += thisTrack.parts[temp.index].getDuration();
          temp.size = thisTrack.parts[temp.index].getSize();//bytesize of frame
          sortSet.insert(temp);
        }
        //remove highest keyPart
        sortSet.erase(keyBegin);
      }

      ///\todo Update this thing for boxes >4G?
      mdatSize = dataSize + 8;//+8 for mp4 header

    }
    header << std::string(moovBox.asBox(), moovBox.boxedSize());


    if (!fragmented) { //if we are making a non fragmented MP4 and there are parts
      char mdatHeader[8] = {0x00,0x00,0x00,0x00,'m','d','a','t'};
      Bit::htobl(mdatHeader, mdatSize);
      header.write(mdatHeader, 8);
    } else {
      //this is a dirty fix to prevent the code from adding 0xDE to the end of the header
      header << (char)(0);
    }

    size += header.str().size();
    if (fragmented) {
      realBaseOffset = header.str().size();
    }
    return header.str();
  }

  /// Calculate a seekPoint, based on byteStart, metadata, tracks and headerSize.
  /// The seekPoint will be set to the timestamp of the first packet to send.
  void OutProgressiveMP4::findSeekPoint(long long byteStart, long long & seekPoint, unsigned int headerSize) {
    seekPoint = 0;
    //if we're starting in the header, seekPoint is always zero.
    if (byteStart <= headerSize) {
      return;
    }
    //okay, we're past the header. Substract the headersize from the starting postion.
    byteStart -= headerSize;
    //forward through the file by headers, until we reach the point where we need to be
    while (!sortSet.empty()) {
      //record where we are
      seekPoint = sortSet.begin()->time;
      //substract the size of this fragment from byteStart
      byteStart -= sortSet.begin()->size;
      //if that put us past the point where we wanted to be, return right now
      if (byteStart < 0) {
        INFO_MSG("We're starting at time %lld, skipping %lld bytes", seekPoint, byteStart+sortSet.begin()->size);
        return;
      }
      //otherwise, set currPos to where we are now and continue
      currPos += sortSet.begin()->size;
      //find the next part
      keyPart temp;
      temp.index = sortSet.begin()->index + 1;
      temp.trackID = sortSet.begin()->trackID;
      if (temp.index < myMeta.tracks[temp.trackID].parts.size()) { //only insert when there are parts left
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
  void OutProgressiveMP4::parseRange(std::string header, long long & byteStart, long long & byteEnd, long long & seekPoint, unsigned int headerSize) {
    if (header.size() < 6 || header.substr(0, 6) != "bytes=") {
      byteEnd = 0;
      DEBUG_MSG(DLVL_WARN, "Invalid range header: %s", header.c_str());
      return;
    }
    header.erase(0, 6);
    if (header.size() && header[0] == '-') {
      //negative range = count from end
      byteStart = 0;
      for (unsigned int i = 1; i < header.size(); ++i) {
        if (header[i] >= '0' && header[i] <= '9') {
          byteStart *= 10;
          byteStart += header[i] - '0';
          continue;
        }
        break;
      }
      if (byteStart > byteEnd) {
        //entire file if starting before byte zero
        byteStart = 0;
        findSeekPoint(byteStart, seekPoint, headerSize);
        return;
      } else {
        //start byteStart bytes before byteEnd
        byteStart = byteEnd - byteStart;
        findSeekPoint(byteStart, seekPoint, headerSize);
        return;
      }
    } else {
      long long size = byteEnd;
      byteEnd = 0;
      byteStart = 0;
      unsigned int i = 0;
      for (; i < header.size(); ++i) {
        if (header[i] >= '0' && header[i] <= '9') {
          byteStart *= 10;
          byteStart += header[i] - '0';
          continue;
        }
        break;
      }
      if (header[i] != '-') {
        DEBUG_MSG(DLVL_WARN, "Invalid range header: %s", header.c_str());
        byteEnd = 0;
        return;
      }
      ++i;
      if (i < header.size()) {
        for (; i < header.size(); ++i) {
          if (header[i] >= '0' && header[i] <= '9') {
            byteEnd *= 10;
            byteEnd += header[i] - '0';
            continue;
          }
          break;
        }
        if (byteEnd > size - 1) {
          byteEnd = size - 1;
        }
      } else {
        byteEnd = size;
      }
      DEBUG_MSG(DLVL_MEDIUM, "Range request: %lli-%lli (%s)", byteStart, byteEnd, header.c_str());
      findSeekPoint(byteStart, seekPoint, headerSize);
      return;
    }
  }

  void OutProgressiveMP4::sendFragmentHeader() {
    long unsigned int dataOffset = 0;
    uint64_t mdatSize = 8;
    MP4::MOOF moofBox;
    MP4::MFHD mfhdBox;
    mfhdBox.setSequenceNumber(fragSeqNum++);
    moofBox.setContent(mfhdBox, 0);
    unsigned int moofIndex = 1;
    sortSet.clear();

    //sort all parts here
    std::set <keyPart> trunOrder;
    //set with trackID, relative data offset, time and size
    for (std::map<long unsigned int, fragSet>::iterator it = currentPartSet.begin(); it != currentPartSet.end(); it++) {
      long unsigned int timeStamp = it->second.firstTime;
      DTSC::Track & thisTrack = myMeta.tracks[it->first];
      for (long unsigned int i = it->second.firstPart; i <= it->second.lastPart; i++) {
        keyPart temp;
        temp.trackID = it->first;
        temp.size = thisTrack.parts[i].getSize();
        temp.duration = thisTrack.parts[i].getDuration();
        temp.time = timeStamp;
        timeStamp += temp.duration;
        temp.endTime = timeStamp;
        temp.timeOffset = thisTrack.parts[i].getOffset();//this will be changed soon, so now it is used for B-frame offset
        trunOrder.insert(temp);
      }
    }

    //now all the parts have been sorted, we make a relative ByteOffset
    long unsigned int relativeOffset = 0;
    for (std::set<keyPart>::iterator it = trunOrder.begin(); it != trunOrder.end(); it++) {
      //We have to make a copy, because altering the element inside the set would invalidate the iterators
      keyPart temp = *it;
      temp.byteOffset = relativeOffset;
      relativeOffset += it->size;
      DONTEVEN_MSG("Anticipating tid: %lu size: %lu", it->trackID, it->size);
      sortSet.insert(temp);
    }
    trunOrder.clear();//erase the trunOrder set, to keep memory usage down


    bool firstSample = true;
    bool hasAudio = false;
    //Fun fact! Firefox cares about the ordering here.
    //It doesn't care about the order or track IDs in the header.
    //But - the first TRAF must be a video TRAF, if video is present.
    std::deque<std::map<long unsigned int, fragSet>::iterator> sortedTracks;
    for (std::map<long unsigned int, fragSet>::iterator it = currentPartSet.begin(); it != currentPartSet.end(); it++) {
      if (myMeta.tracks[it->first].type == "video"){
        sortedTracks.push_front(it);
      }else{
        if (myMeta.tracks[it->first].type == "audio"){
          hasAudio = true;
        }
        sortedTracks.push_back(it);
      }
    }

    for (std::deque<std::map<long unsigned int, fragSet>::iterator>::iterator ti = sortedTracks.begin(); ti != sortedTracks.end(); ++ti) {
      std::map<long unsigned int, fragSet>::iterator & it = *ti;
      unsigned int tid = it->first;
      DTSC::Track & thisTrack = myMeta.tracks[tid];
      MP4::TRAF trafBox;
      MP4::TFHD tfhdBox;
      tfhdBox.setFlags(MP4::tfhdBaseOffset | MP4::tfhdSampleDura | MP4::tfhdSampleSize | MP4::tfhdSampleFlag);
      tfhdBox.setTrackID(tid);
      tfhdBox.setBaseDataOffset(realBaseOffset - 1); //Offset of current moof box, we use currPos for this. Not sure why we need the -1, but this gives the right offset
      tfhdBox.setDefaultSampleDuration(thisTrack.parts[it->second.firstPart].getDuration());
      tfhdBox.setDefaultSampleSize(thisTrack.parts[it->second.firstPart].getSize());
      if (tid == vidTrack) {
        tfhdBox.setDefaultSampleFlags(MP4::noIPicture | MP4::noKeySample);
      } else {
        tfhdBox.setDefaultSampleFlags(MP4::isIPicture | MP4::isKeySample);
      }
      trafBox.setContent(tfhdBox, 0);

      unsigned int trafOffset = 1;
      for (std::set<keyPart>::iterator trunIt = sortSet.begin(); trunIt != sortSet.end(); trunIt++) {
        if (trunIt->trackID == tid) {
          MP4::TRUN trunBox;
          trunBox.setFlags(MP4::trundataOffset | MP4::trunfirstSampleFlags | MP4::trunsampleSize | MP4::trunsampleDuration | (trunIt->timeOffset ? MP4::trunsampleOffsets : 0));
          //The value set here, will be updated afterwards to the correct value
          trunBox.setDataOffset(trunIt->byteOffset);
          trunBox.setFirstSampleFlags(MP4::isIPicture | (firstSample ? MP4::isKeySample : MP4::noKeySample));
          firstSample = false;

          mdatSize += trunIt->size;

          MP4::trunSampleInformation sampleInfo;
          sampleInfo.sampleSize = trunIt->size;
          sampleInfo.sampleDuration = trunIt->duration;
          if (trunIt->timeOffset) {
            sampleInfo.sampleOffset = trunIt->timeOffset;
          }
          trunBox.setSampleInformation(sampleInfo, 0);
          trafBox.setContent(trunBox, trafOffset++);
        }
      }
      moofBox.setContent(trafBox, moofIndex);
      moofIndex++;
    }

    //Oh god why do we do this.
    if (chromeWorkaround && hasAudio && fragSeqNum == 1){
      MP4::TRAF trafBox;
      MP4::TRUN trunBox;
      trunBox.setFlags(MP4::trundataOffset | MP4::trunfirstSampleFlags | MP4::trunsampleSize | MP4::trunsampleDuration);
      trunBox.setDataOffset(0);
      trunBox.setFirstSampleFlags(MP4::isIPicture | MP4::noKeySample);
      MP4::trunSampleInformation sampleInfo;
      sampleInfo.sampleSize = 0;
      sampleInfo.sampleDuration = -1;
      trunBox.setSampleInformation(sampleInfo, 0);
      trafBox.setContent(trunBox, 0);
      moofBox.setContent(trafBox, moofIndex);
      moofIndex++;
    }


    //Update the trun data offsets with their correct values
    MP4::TRAF loopTrafBox;
    MP4::TRUN fixTrunBox;
    uint32_t moofCount = moofBox.getContentCount();
    for (unsigned int i = 0; i < moofCount; i++) {
      if (moofBox.getContent(i).isType("traf")) {
        loopTrafBox = ((MP4::TRAF &)moofBox.getContent(i, true));
        uint32_t trafCount = loopTrafBox.getContentCount();
        for (unsigned int j = 0; j < trafCount; j++) {
          MP4::Box & tmpBox = loopTrafBox.getContent(j, true);
          if (tmpBox.isType("trun")) {
            fixTrunBox = (MP4::TRUN &)tmpBox;
            fixTrunBox.setDataOffset(fixTrunBox.getDataOffset() + moofBox.boxedSize() + 8);
          }
        }
      }
    }
    realBaseOffset += (moofBox.boxedSize() + mdatSize);
    myConn.SendNow(moofBox.asBox(), moofBox.boxedSize());
    char mdatHeader[8] = {0x00,0x00,0x00,0x00,'m','d','a','t'};
    Bit::htobl(mdatHeader, mdatSize);
    myConn.SendNow(mdatHeader, 8);
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

    chromeWorkaround = (H.GetHeader("User-Agent").find("Chrome") != std::string::npos && H.GetHeader("User-Agent").find("Edge") == std::string::npos && H.GetHeader("User-Agent").find("OPR/") == std::string::npos);

    /*LTS-START*/
    //allow setting of max lead time through buffer variable.
    //max lead time is set in MS, but the variable is in integer seconds for simplicity.
    if (H.GetVar("buffer") != "") {
      maxSkipAhead = JSON::Value(H.GetVar("buffer")).asInt() * 1000;
      minSkipAhead = maxSkipAhead - std::min(2500u, maxSkipAhead / 2);
    }
    //allow setting of play back rate through buffer variable.
    //play back rate is set in MS per second, but the variable is a simple multiplier.
    if (H.GetVar("rate") != ""){
      long long int multiplier = JSON::Value(H.GetVar("rate")).asInt();
      if (multiplier){
        realTime = 1000 / multiplier;
      }else{
        realTime = 0;
      }
    }
    /*LTS-END*/
    //Always initialize before anything else
    initialize();

    //Make sure we start receiving data after this function
    ///\todo Should this happen here?
    parseData = true;
    wantRequest = false;
    sentHeader = false;

    //Check if the url contains .3gp --> if yes, we will send a 3gp header
    sending3GP = (H.url.find(".3gp") != std::string::npos);

    //For storing the header.
    ///\todo Do we really need this though?
    std::string headerData = DTSCMeta2MP4Header(fileSize, myMeta.live);

    seekPoint = 0;
    if (myMeta.live) {
      //for live we use fragmented mode
      fragSeqNum = 0;
      partListSent = 0;
      partListLength = 0;
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
    if (!myMeta.live) {
      if (H.GetHeader("Range") != "") {
        parseRange(H.GetHeader("Range"), byteStart, byteEnd, seekPoint, headerData.size());
        rangeType = H.GetHeader("Range")[0];
      }
    }
    H.Clean(); //make sure no parts of old requests are left in any buffers
    H.setCORSHeaders();
    H.SetHeader("Content-Type", "video/MP4"); //Send the correct content-type for MP4 files
    if (!myMeta.live) {
      H.SetHeader("Accept-Ranges", "bytes, parsec");
    }
    if (rangeType != ' ') {
      if (!byteEnd) {
        if (rangeType == 'p') {
          H.SetBody("Starsystem not in communications range");
          H.SendResponse("416", "Starsystem not in communications range", myConn);
          return;
        } else {
          H.SetBody("Requested Range Not Satisfiable");
          H.SendResponse("416", "Requested Range Not Satisfiable", myConn);
          return;
        }
      } else {
        std::stringstream rangeReply;
        rangeReply << "bytes " << byteStart << "-" << byteEnd << "/" << fileSize;
        H.SetHeader("Content-Length", byteEnd - byteStart + 1);
        //do not multiplex requests that are > 1MiB
        if (byteEnd - byteStart + 1 > 1024 * 1024) {
          H.SetHeader("MistMultiplex", "No");
        }
        H.SetHeader("Content-Range", rangeReply.str());
        /// \todo Switch to chunked?
        H.SendResponse("206", "Partial content", myConn);
        //H.StartResponse("206", "Partial content", HTTP_R, conn);
      }
    } else {
      if (!myMeta.live) {
        H.SetHeader("Content-Length", byteEnd - byteStart + 1);
      }
      //do not multiplex requests that aren't ranged
      H.SetHeader("MistMultiplex", "No");
      /// \todo Switch to chunked?
      H.SendResponse("200", "OK", myConn);
      //HTTP_S.StartResponse(HTTP_R, conn);
    }
    leftOver = byteEnd - byteStart + 1;//add one byte, because range "0-0" = 1 byte of data
    if (byteStart < (long long)headerData.size()) {
      /// \todo Switch to chunked?
      myConn.SendNow(headerData.data() + byteStart, std::min((long long)headerData.size(), byteEnd) - byteStart); //send MP4 header
      leftOver -= std::min((long long)headerData.size(), byteEnd) - byteStart;
    }
    currPos += headerData.size();//we're now guaranteed to be past the header point, no matter what
  }

///Builds up a datastructure that allows for access in the fragment send header function
///using the fragment number **FOR THIS USER, NOT ACTUAL FRAGMENT NUMBER, HAS NOTHING TO DO WITH ACTUAL FRAGMENTS EVEN**
///We take the corresponding keyframe and interframes of the main video track and take concurrent frames from its secondary (audio) tracks
///\todo See if we can use something more elegant than a member variable...
  void OutProgressiveMP4::buildFragment() {
    DTSC::Key & currKey = myMeta.tracks[vidTrack].getKey(getKeyForTime(vidTrack, thisPacket.getTime()));
    long long int startms = thisPacket.getTime();
    if (!needsLookAhead){
      needsLookAhead = 1000;
      currentPartSet.clear();
      return;
    }
    long long int endms = startms + needsLookAhead;
    bool missingSome = true;

    while (missingSome){
      missingSome = false;
      currentPartSet.clear();
      for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
        DTSC::Track & thisTrack = myMeta.tracks[*it];
        fragSet thisRange;
        thisRange.firstPart = 0;
        thisRange.firstTime = thisTrack.keys.begin()->getTime();
        unsigned long long int prevParts = 0;
        for (std::deque<DTSC::Key>::iterator it2 = thisTrack.keys.begin(); it2 != thisTrack.keys.end(); it2++) {
          if (it2->getTime() > startms) {
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
        size_t maxParts = thisTrack.parts.size(); 
        for (size_t i = thisRange.firstPart; i < maxParts; i++) {
          if (first && curMS >= startms) {
            thisRange.firstPart = i;
            thisRange.firstTime = curMS;
            first = false;
          }
          uint32_t pDur = thisTrack.parts[i].getDuration();
          nextMS = curMS + pDur;
          //Make sure we always look ahead at least a single frame
          if (pDur > needsLookAhead){
            needsLookAhead = pDur;
            INFO_MSG("Slow frame! Increasing lookAhead to %lums", needsLookAhead);
          }
          thisRange.lastPart = i;
          thisRange.lastTime = curMS;
          if (nextMS >= endms){break;}
          curMS = nextMS;
        }
        if (first){
          endms = thisTrack.lastms;
          if (needsLookAhead != endms - startms){
            needsLookAhead = endms - startms;
            INFO_MSG("False start! Increasing lookAhead to %lums", needsLookAhead);
            missingSome = true;
          }
          break;
        }
        currentPartSet[*it] = thisRange;
      }
    }
  }

  void OutProgressiveMP4::sendNext() {
    static bool perfect = true;
    
    //Obtain a pointer to the data of this packet
    char * dataPointer = 0;
    unsigned int len = 0;
    thisPacket.getString("data", dataPointer, len);



    if (myMeta.live) {
      //if header needed
      if (!partListLength || partListSent >= partListLength) {
        DTSC::Track & mainTrk = myMeta.tracks[vidTrack];
        // The extra 600ms here is for the metadata sync delay.
        // It can be removed once we get rid of that.
        // (sync delay = ~1s, minimum lookAhead is 420ms -> ~600ms extra needed)
        if (fragSeqNum > 10 && thisPacket.getTime() + needsLookAhead + 600 < mainTrk.keys.rbegin()->getTime() && mainTrk.lastms - mainTrk.keys.rbegin()->getTime() > needsLookAhead){
          INFO_MSG("Skipping forward %llums (%llu ms LA)", mainTrk.keys.rbegin()->getTime() - thisPacket.getTime(), needsLookAhead);
          seek(mainTrk.keys.rbegin()->getTime());
          return;
        }
        //building set first
        buildFragment();//map with metadata for keyframe
        sendFragmentHeader();
        partListSent = 0;
        partListLength = 0;
        for (std::map<long unsigned int, fragSet>::iterator it = currentPartSet.begin(); it != currentPartSet.end(); it++) {
          partListLength += it->second.lastPart - it->second.firstPart + 1;
        }
      }
      //generate content in mdat, meaning: send right parts
      DONTEVEN_MSG("Sending tid: %ld size: %u", thisPacket.getTrackId() , len);
      myConn.SendNow(dataPointer, len);
      partListSent++;
    }

    if ((unsigned long)thisPacket.getTrackId() != sortSet.begin()->trackID || thisPacket.getTime() != sortSet.begin()->time || len != sortSet.begin()->size) {
      if (thisPacket.getTime() > sortSet.begin()->time || (unsigned long)thisPacket.getTrackId() > sortSet.begin()->trackID) {
        if (perfect) {
          WARN_MSG("Warning: input is inconsistent. Expected %lu:%llu but got %ld:%llu - cancelling playback", sortSet.begin()->trackID, sortSet.begin()->time, thisPacket.getTrackId(), thisPacket.getTime());
          perfect = false;
          myConn.close();
        }
      } else {
        WARN_MSG("Did not receive expected %lu:%llu but got %ld:%llu - throwing it away", sortSet.begin()->trackID, sortSet.begin()->time, thisPacket.getTrackId(), thisPacket.getTime());
      }
      return;
    }

    //The remainder of this function handles non-live situations
    if (myMeta.live){
      sortSet.erase(sortSet.begin());
      return;
    }

    if (currPos >= byteStart) {
      myConn.SendNow(dataPointer, std::min(leftOver, (long long)len));
      leftOver -= len;
    } else {
      if (currPos + (long long)len > byteStart) {
        myConn.SendNow(dataPointer + (byteStart - currPos), std::min(leftOver, (long long)(len - (byteStart - currPos))));
        leftOver -= len - (byteStart - currPos);
      }
    }

    //keep track of where we are
    if (!sortSet.empty()) {
      keyPart temp;
      temp.index = sortSet.begin()->index + 1;
      temp.trackID = sortSet.begin()->trackID;
      if (temp.index < myMeta.tracks[temp.trackID].parts.size()) { //only insert when there are parts left
        temp.time = sortSet.begin()->endTime;//timeplace of frame
        temp.endTime = sortSet.begin()->endTime + myMeta.tracks[temp.trackID].parts[temp.index].getDuration();
        temp.size = myMeta.tracks[temp.trackID].parts[temp.index].getSize();//bytesize of frame
        sortSet.insert(temp);
      }
      currPos += sortSet.begin()->size;
      //remove highest keyPart
      sortSet.erase(sortSet.begin());
    }



    if (leftOver < 1) {

      //stop playback, wait for new request
      stop();
      wantRequest = true;
    }
  }

  void OutProgressiveMP4::sendHeader() {
    if (myMeta.live) {
      vidTrack = getMainSelectedTrack();
      bool reSeek = false;
      DTSC::Track & Trk = myMeta.tracks[vidTrack];
      for (int i = 0; i < Trk.parts.size(); i++) {
        uint32_t pDur = Trk.parts[i].getDuration();
        //Make sure we always look ahead at least a single frame
        if (pDur > needsLookAhead){
          needsLookAhead = pDur;
          reSeek = true;
        }
      }
      if (reSeek){
        INFO_MSG("Increased initial lookAhead of %lums", needsLookAhead);
        initialSeek();
      }
    }else{
      seek(seekPoint);
    }
    sentHeader = true;
  }

}
