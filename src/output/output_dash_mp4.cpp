#include "output_dash_mp4.h"
#include <mist/defines.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
#include <mist/mp4_dash.h>
#include <mist/checksum.h>
#include <mist/timing.h>

namespace Mist {
  OutDashMP4::OutDashMP4(Socket::Connection & conn) : HTTPOutput(conn){realTime = 0;}
  OutDashMP4::~OutDashMP4(){}
  
  std::string OutDashMP4::makeTime(long long unsigned int time){
    std::stringstream r;
    r << "PT" << (((time / 1000) / 60) /60) << "H" << ((time / 1000) / 60) % 60 << "M" << (time / 1000) % 60 << "." << time % 1000 / 10 << "S";
    return r.str();
  }
  
  void OutDashMP4::buildFtyp(unsigned int tid){
    H.Chunkify("\000\000\000", 3, myConn);
    H.Chunkify("\040", 1, myConn);
    H.Chunkify("ftypisom\000\000\000\000isom", 16, myConn);
    if (myMeta.tracks[tid].type == "video"){
      H.Chunkify("avc1", 4, myConn);
    }else{
      H.Chunkify("M4A ", 4, myConn);
    }
    H.Chunkify("mp42dash", 8, myConn);
  }

  std::string OutDashMP4::buildMoov(unsigned int tid){
    std::string trackType = myMeta.tracks[tid].type;
    MP4::MOOV moovBox;
    
    MP4::MVHD mvhdBox(0);
    mvhdBox.setTrackID(2);
    mvhdBox.setDuration(0xFFFFFFFF);
    moovBox.setContent(mvhdBox, 0);

    MP4::IODS iodsBox;
    if (trackType == "video"){
      iodsBox.setODVideoLevel(0xFE);
    }else{
      iodsBox.setODAudioLevel(0xFE);
    }
    moovBox.setContent(iodsBox, 1);
    
    
    MP4::MVEX mvexBox;
    MP4::MEHD mehdBox;
    mehdBox.setFragmentDuration(0xFFFFFFFF);
    mvexBox.setContent(mehdBox, 0);
    MP4::TREX trexBox;
    trexBox.setTrackID(1);
    mvexBox.setContent(trexBox, 1);
    moovBox.setContent(mvexBox, 2);
    
    MP4::TRAK trakBox;
    MP4::TKHD tkhdBox(1, 0, myMeta.tracks[tid].width, myMeta.tracks[tid].height);
    tkhdBox.setFlags(3);
    if (trackType == "audio"){
      tkhdBox.setVolume(256);
      tkhdBox.setWidth(0);
      tkhdBox.setHeight(0);
    }
    tkhdBox.setDuration(0xFFFFFFFF);
    trakBox.setContent(tkhdBox, 0);
    
    MP4::MDIA mdiaBox;
    MP4::MDHD mdhdBox(0);
    mdhdBox.setLanguage(0x44);
    mdhdBox.setDuration(myMeta.tracks[tid].lastms);
    mdiaBox.setContent(mdhdBox, 0);
    
    if (trackType == "video"){
      MP4::HDLR hdlrBox(myMeta.tracks[tid].type,"VideoHandler");
      mdiaBox.setContent(hdlrBox, 1);
    }else{
      MP4::HDLR hdlrBox(myMeta.tracks[tid].type,"SoundHandler");
      mdiaBox.setContent(hdlrBox, 1);
    }

    MP4::MINF minfBox;
    MP4::DINF dinfBox;
    MP4::DREF drefBox;
    dinfBox.setContent(drefBox, 0);
    minfBox.setContent(dinfBox, 0);
    
    MP4::STBL stblBox;
    MP4::STSD stsdBox;
    stsdBox.setVersion(0);
    
    if (myMeta.tracks[tid].codec == "H264"){
      MP4::AVC1 avc1Box;
      avc1Box.setWidth(myMeta.tracks[tid].width);
      avc1Box.setHeight(myMeta.tracks[tid].height);
      
      MP4::AVCC avccBox;
      avccBox.setPayload(myMeta.tracks[tid].init);
      avc1Box.setCLAP(avccBox);
      stsdBox.setEntry(avc1Box, 0);
    }
    if (myMeta.tracks[tid].codec == "HEVC"){
      MP4::HEV1 hev1Box;
      hev1Box.setWidth(myMeta.tracks[tid].width);
      hev1Box.setHeight(myMeta.tracks[tid].height);
      
      MP4::HVCC hvccBox;
      hvccBox.setPayload(myMeta.tracks[tid].init);
      hev1Box.setCLAP(hvccBox);
      stsdBox.setEntry(hev1Box, 0);
    }
    if (myMeta.tracks[tid].codec == "AAC" || myMeta.tracks[tid].codec == "MP3"){
      MP4::AudioSampleEntry ase;
      ase.setCodec("mp4a");
      ase.setDataReferenceIndex(1);
      ase.setSampleRate(myMeta.tracks[tid].rate);
      ase.setChannelCount(myMeta.tracks[tid].channels);
      ase.setSampleSize(myMeta.tracks[tid].size);
      MP4::ESDS esdsBox(myMeta.tracks[tid].init);
      ase.setCodecBox(esdsBox);
      stsdBox.setEntry(ase,0);
    }
    if (myMeta.tracks[tid].codec == "AC3"){
      ///\todo Note: this code is copied, note for muxing seperation
      MP4::AudioSampleEntry ase;
      ase.setCodec("ac-3");
      ase.setDataReferenceIndex(1);
      ase.setSampleRate(myMeta.tracks[tid].rate);
      ase.setChannelCount(myMeta.tracks[tid].channels);
      ase.setSampleSize(myMeta.tracks[tid].size);
      MP4::DAC3 dac3Box;
      switch (myMeta.tracks[tid].rate){
        case 48000:
          dac3Box.setSampleRateCode(0);
          break;
        case 44100:
          dac3Box.setSampleRateCode(1);
          break;
        case 32000:
          dac3Box.setSampleRateCode(2);
          break;
        default:
          dac3Box.setSampleRateCode(3);
          break;
      }
      /// \todo the next settings are set to generic values, we might want to make these flexible
      dac3Box.setBitStreamIdentification(8);//check the docs, this is a weird property
      dac3Box.setBitStreamMode(0);//set to main, mixed audio
      dac3Box.setAudioConfigMode(2);///\todo find out if ACMode should be different
      if (myMeta.tracks[tid].channels > 4){
        dac3Box.setLowFrequencyEffectsChannelOn(1);
      }else{
        dac3Box.setLowFrequencyEffectsChannelOn(0);
      }
      dac3Box.setFrameSizeCode(20);//should be OK, but test this.
      ase.setCodecBox(dac3Box);
    }
    
    stblBox.setContent(stsdBox, 0);
    
    MP4::STTS sttsBox;
    sttsBox.setVersion(0);
    stblBox.setContent(sttsBox, 1);
    
    MP4::STSC stscBox;
    stscBox.setVersion(0);
    stblBox.setContent(stscBox, 2);
    
    MP4::STCO stcoBox;
    stcoBox.setVersion(0);
    stblBox.setContent(stcoBox, 3);
    
    MP4::STSZ stszBox;
    stszBox.setVersion(0);
    stblBox.setContent(stszBox, 4);
    
    minfBox.setContent(stblBox, 1);
    
    if (trackType == "video"){
      MP4::VMHD vmhdBox;
      vmhdBox.setFlags(1);
      minfBox.setContent(vmhdBox, 2);
    }else{
      MP4::SMHD smhdBox;
      minfBox.setContent(smhdBox, 2);
    }
    
    mdiaBox.setContent(minfBox, 2);
    
    trakBox.setContent(mdiaBox, 1);
    
    moovBox.setContent(trakBox, 3);
    
    return std::string(moovBox.asBox(),moovBox.boxedSize());
  }
    
  std::string OutDashMP4::buildMoof(unsigned int tid, unsigned int keyNum){
    MP4::MOOF moofBox;
    
    MP4::MFHD mfhdBox;
    mfhdBox.setSequenceNumber(keyNum);
    moofBox.setContent(mfhdBox, 0);
    
    MP4::TRAF trafBox;
    MP4::TFHD tfhdBox;
    if (myMeta.tracks[tid].codec == "H264" || myMeta.tracks[tid].codec == "HEVC"){
      tfhdBox.setTrackID(1);
    }
    if (myMeta.tracks[tid].codec == "AAC" || myMeta.tracks[tid].codec == "AC3" || myMeta.tracks[tid].codec == "MP3"){
      tfhdBox.setFlags(MP4::tfhdSampleFlag);
      tfhdBox.setTrackID(1);
      tfhdBox.setDefaultSampleFlags(MP4::isKeySample);
    }
    trafBox.setContent(tfhdBox, 0);
    
    MP4::TFDT tfdtBox;
    ///\todo Determine index for live
    tfdtBox.setBaseMediaDecodeTime(myMeta.tracks[tid].getKey(keyNum).getTime());
    trafBox.setContent(tfdtBox, 1);
    
    int i = 0;
    
    for (int j = 0; j < myMeta.tracks[tid].keys.size(); j++){
      if (myMeta.tracks[tid].keys[j].getNumber() >= keyNum){
        break;
      }
      i += myMeta.tracks[tid].keys[j].getParts();
    }

    MP4::TRUN trunBox;
    if (myMeta.tracks[tid].codec == "H264"){
      trunBox.setFlags(MP4::trundataOffset | MP4::trunsampleSize | MP4::trunsampleDuration | MP4::trunfirstSampleFlags | MP4::trunsampleOffsets);
      trunBox.setFirstSampleFlags(MP4::isKeySample);
      trunBox.setDataOffset(88 + (12 * myMeta.tracks[tid].getKey(keyNum).getParts()) + 8);

      MP4::AVCC avccBox;
      avccBox.setPayload(myMeta.tracks[tid].init);
      for (int j = 0; j < myMeta.tracks[tid].getKey(keyNum).getParts(); j++){
        MP4::trunSampleInformation trunEntry;
        if (!j){
          trunEntry.sampleSize = myMeta.tracks[tid].parts[i].getSize() + 14 + avccBox.getSPSLen() + avccBox.getPPSLen();
        }else{
          trunEntry.sampleSize = myMeta.tracks[tid].parts[i].getSize();
        }
        trunEntry.sampleDuration = myMeta.tracks[tid].parts[i].getDuration();
        trunEntry.sampleOffset = myMeta.tracks[tid].parts[i].getOffset();
        trunBox.setSampleInformation(trunEntry, j);
        i++;
      }
    }
    if (myMeta.tracks[tid].codec == "HEVC"){
      trunBox.setFlags(MP4::trundataOffset | MP4::trunsampleSize | MP4::trunsampleDuration | MP4::trunfirstSampleFlags | MP4::trunsampleOffsets);
      trunBox.setFirstSampleFlags(MP4::isKeySample);
      trunBox.setDataOffset(88 + (12 * myMeta.tracks[tid].getKey(keyNum).getParts()) + 8);

      MP4::HVCC hvccBox;
      hvccBox.setPayload(myMeta.tracks[tid].init);
      std::deque<MP4::HVCCArrayEntry> content = hvccBox.getArrays();
      for (int j = 0; j < myMeta.tracks[tid].getKey(keyNum).getParts(); j++){
        MP4::trunSampleInformation trunEntry;
        trunEntry.sampleSize = myMeta.tracks[tid].parts[i].getSize();
        if (!j){
          for (std::deque<MP4::HVCCArrayEntry>::iterator it = content.begin(); it != content.end(); it++){
            for (std::deque<std::string>::iterator it2 = it->nalUnits.begin(); it2 != it->nalUnits.end(); it2++){
              trunEntry.sampleSize += 4 + (*it2).size();
            }
          }
        }
        trunEntry.sampleDuration = myMeta.tracks[tid].parts[i].getDuration();
        trunEntry.sampleOffset = myMeta.tracks[tid].parts[i].getOffset();
        trunBox.setSampleInformation(trunEntry, j);
        i++;
      }
    }
    if (myMeta.tracks[tid].codec == "AAC" || myMeta.tracks[tid].codec == "AC3" || myMeta.tracks[tid].codec == "MP3"){
      trunBox.setFlags(MP4::trundataOffset | MP4::trunsampleSize | MP4::trunsampleDuration);
      trunBox.setDataOffset(88 + (8 * myMeta.tracks[tid].getKey(keyNum).getParts()) + 8);
      for (int j = 0; j < myMeta.tracks[tid].getKey(keyNum).getParts(); j++){
        MP4::trunSampleInformation trunEntry;
        trunEntry.sampleSize = myMeta.tracks[tid].parts[i].getSize();
        trunEntry.sampleDuration = myMeta.tracks[tid].parts[i].getDuration();
        trunBox.setSampleInformation(trunEntry, j);
        i++;
      }
    }
    trafBox.setContent(trunBox, 2);
   
    moofBox.setContent(trafBox, 1);
    
    return std::string(moofBox.asBox(), moofBox.boxedSize());
  }
  
  std::string OutDashMP4::buildNalUnit(unsigned int len, const char * data){
    std::stringstream r;
    r << (char)((len >> 24) & 0xFF);
    r << (char)((len >> 16) & 0xFF);
    r << (char)((len >> 8) & 0xFF);
    r << (char)((len) & 0xFF);
    r << std::string(data, len);
    return r.str();
  }
  
  void OutDashMP4::buildMdat(unsigned int tid, unsigned int keyNum){
    unsigned int size = 8;
    unsigned int curPart = 0;
    for (unsigned int i = 0; i < myMeta.tracks[tid].keys.size(); ++i){
      if (myMeta.tracks[tid].keys[i].getNumber() >= keyNum){break;}
      curPart += myMeta.tracks[tid].keys[i].getParts();
    }
    for (int i = 0; i < myMeta.tracks[tid].getKey(keyNum).getParts(); i++){
      size += myMeta.tracks[tid].parts[curPart++].getSize();
    }
    if (myMeta.tracks[tid].codec == "H264"){
      MP4::AVCC avccBox;
      avccBox.setPayload(myMeta.tracks[tid].init);
      size += 14 + avccBox.getSPSLen() + avccBox.getPPSLen();
    }
    if (myMeta.tracks[tid].codec == "HEVC"){
      MP4::HVCC hvccBox;
      hvccBox.setPayload(myMeta.tracks[tid].init);
      std::deque<MP4::HVCCArrayEntry> content = hvccBox.getArrays();
      for (std::deque<MP4::HVCCArrayEntry>::iterator it = content.begin(); it != content.end(); it++){
        for (std::deque<std::string>::iterator it2 = it->nalUnits.begin(); it2 != it->nalUnits.end(); it2++){
          size += 4 + (*it2).size();
        }
      }
    }
    char mdatstr[8] = {0, 0, 0, 0, 'm', 'd', 'a', 't'};
    mdatstr[0] = (char)((size >> 24) & 0xFF);
    mdatstr[1] = (char)((size >> 16) & 0xFF);
    mdatstr[2] = (char)((size >> 8) & 0xFF);
    mdatstr[3] = (char)((size) & 0xFF);
    H.Chunkify(mdatstr, 8, myConn);
    selectedTracks.clear();
    selectedTracks.insert(tid);
    seek(myMeta.tracks[tid].getKey(keyNum).getTime());
    std::string init;
    char *  data;
    unsigned int dataLen;
    if (myMeta.tracks[tid].codec == "H264"){
      MP4::AVCC avccBox;
      avccBox.setPayload(myMeta.tracks[tid].init);
      init = buildNalUnit(2, "\011\340");
      H.Chunkify(init, myConn);//09E0
      init = buildNalUnit(avccBox.getSPSLen(), avccBox.getSPS());
      H.Chunkify(init, myConn);
      init = buildNalUnit(avccBox.getPPSLen(), avccBox.getPPS());
      H.Chunkify(init, myConn);
    }
    if (myMeta.tracks[tid].codec == "HEVC"){
      MP4::HVCC hvccBox;
      hvccBox.setPayload(myMeta.tracks[tid].init);
      std::deque<MP4::HVCCArrayEntry> content = hvccBox.getArrays();
      for (int j = 0; j < myMeta.tracks[tid].getKey(keyNum).getParts(); j++){
        for (std::deque<MP4::HVCCArrayEntry>::iterator it = content.begin(); it != content.end(); it++){
          for (std::deque<std::string>::iterator it2 = it->nalUnits.begin(); it2 != it->nalUnits.end(); it2++){
            init = buildNalUnit((*it2).size(), (*it2).c_str());
            H.Chunkify(init, myConn);
          }
        }
      }
    }
    for (int i = 0; i < myMeta.tracks[tid].getKey(keyNum).getParts(); i++){
      prepareNext();
      thisPacket.getString("data", data, dataLen);
      H.Chunkify(data, dataLen, myConn);
    }
    return;
  }

  std::string OutDashMP4::h264init(const std::string & initData) {
    std::stringstream r;
    MP4::AVCC avccBox;
    avccBox.setPayload(initData);
    r << std::hex << std::setw(2) << std::setfill('0') << (int)avccBox.getSPS()[0] << std::dec;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)avccBox.getSPS()[1] << std::dec;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)avccBox.getSPS()[2] << std::dec;
    return r.str();
  }

  std::string OutDashMP4::h265init(const std::string & initData) {
    std::stringstream r;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[1] << std::dec;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[6] << std::dec;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[7] << std::dec;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[8] << std::dec;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[9] << std::dec;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[10] << std::dec;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[11] << std::dec;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[12] << std::dec;
    return r.str();
  }
    
  std::string OutDashMP4::buildManifest(){
    initialize();
    int lastVidTime = 0;
    int vidKeys = 0;
    int vidInitTrack = 0;
    int lastAudTime = 0;
    int audKeys = 0;
    int audInitTrack = 0;
    ///\todo Dash automatically selects the last audio and video track for manifest, maybe make this expandable/selectable?
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it ++){
      if ((it->second.codec == "H264" || it->second.codec == "HEVC") && it->second.lastms > lastVidTime){
        lastVidTime = it->second.lastms;
        vidKeys = it->second.keys.size();
        vidInitTrack = it->first;
      }
      if ((it->second.codec == "AAC" || it->second.codec == "MP3" || it->second.codec == "AC3")&& it->second.lastms > lastAudTime){
        lastAudTime = it->second.lastms;
        audKeys = it->second.keys.size();
        audInitTrack = it->first;
      }
    }
    std::stringstream r;
    r << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl;
    r << "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 http://standards.iso.org/ittf/PubliclyAvailableStandards/MPEG-DASH_schema_files/DASH-MPD.xsd\" profiles=\"urn:mpeg:dash:profile:isoff-live:2011\" ";
    if (myMeta.vod){
      r << "type=\"static\" mediaPresentationDuration=\"" << makeTime(std::max(lastVidTime, lastAudTime)) << "\" minBufferTime=\"PT1.5S\"";
    }else{
      r << "type=\"dynamic\" minimumUpdatePeriod=\"PT1.0S\" availabilityStartTime=\"" << Util::getUTCString(Util::epoch() - std::max(lastVidTime, lastAudTime)/1000) << "\" ";
      int bufferTime = myMeta.tracks.begin()->second.lastms - myMeta.tracks.begin()->second.firstms;
      r << "timeShiftBufferDepth=\"PT" << bufferTime / 1000 << "." << bufferTime % 1000 << "S\" suggestedPresentationDelay=\"PT15.0S\" minBufferTime=\"PT6.0S\"";
    }
    r << " >" << std::endl;
    r << "  <ProgramInformation><Title>" << streamName << "</Title></ProgramInformation>" << std::endl;
    r << "  <Period ";
    if (myMeta.live){
      r << "id=\"0\" ";
    }
    r<< "start=\"PT0S\">" << std::endl;
    if (vidInitTrack){
      DTSC::Track & trackRef = myMeta.tracks[vidInitTrack];
      r << "    <AdaptationSet id=\"0\" mimeType=\"video/mp4\" width=\"" << trackRef.width << "\" height=\"" << trackRef.height << "\" frameRate=\"" << trackRef.fpks / 1000 << "\" segmentAlignment=\"true\" startWithSAP=\"1\" subsegmentAlignment=\"true\" subsegmentStartsWithSAP=\"1\">" << std::endl;
      r << "      <SegmentTemplate timescale=\"1000\" media=\"chunk_$RepresentationID$_$Time$.m4s\" initialization=\"chunk_$RepresentationID$_init.m4s\">" << std::endl;
      r << "        <SegmentTimeline>" << std::endl;
      r <<"          <S t=\"" << trackRef.firstms << "\" d=\"" << trackRef.keys[0].getLength() << "\" />" << std::endl;
      for (int i = 1; i < trackRef.keys.size() - 1; i++){
        r << "          <S d=\"" << trackRef.keys[i].getLength() << "\" />" << std::endl;
      }
      if (myMeta.vod){
        int lastDur = trackRef.lastms - trackRef.keys.rbegin()->getTime();
        r << "          <S d=\"" << lastDur << "\" />" << std::endl;
      }
      r << "        </SegmentTimeline>" << std::endl;
      r << "      </SegmentTemplate>" << std::endl;
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        if (it->second.codec == "H264"){
          r << "      <Representation ";
          r << "id=\"" << it->first << "\" ";
          r << "codecs=\"avc1." << h264init(it->second.init) << "\" ";
          r << "bandwidth=\"" << it->second.bps << "\" ";
          r << "/>" << std::endl;
        }
        if (it->second.codec == "HEVC"){
          r << "      <Representation ";
          r << "id=\"" << it->first << "\" ";
          r << "codecs=\"hev1." << h265init(it->second.init) << "\" ";
          r << "bandwidth=\"" << it->second.bps << "\" ";
          r << "/>" << std::endl;
        }
      }
      r << "    </AdaptationSet>" << std::endl;
    }
    if (audInitTrack){
      DTSC::Track & trackRef = myMeta.tracks[audInitTrack];
      r << "    <AdaptationSet id=\"1\" mimeType=\"audio/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\" subsegmentAlignment=\"true\" subsegmentStartsWithSAP=\"1\" >" << std::endl;
      r << "      <Role schemeIdUri=\"urn:mpeg:dash:role:2011\" value=\"main\"/>" << std::endl;
      r << "      <SegmentTemplate timescale=\"1000\" media=\"chunk_$RepresentationID$_$Time$.m4s\" initialization=\"chunk_$RepresentationID$_init.m4s\">" << std::endl;

      r << "        <SegmentTimeline>" << std::endl;
      r <<"          <S t=\"" << trackRef.firstms << "\" d=\"" << trackRef.keys[0].getLength() << "\" />" << std::endl;
      for (int i = 1; i < trackRef.keys.size() - 1; i++){
        r << "          <S d=\"" << trackRef.keys[i].getLength() << "\" />" << std::endl;
      }
      if (myMeta.vod){
        int lastDur = trackRef.lastms - trackRef.keys.rbegin()->getTime();
        r << "          <S d=\"" << lastDur << "\" />" << std::endl;
      }
      r << "        </SegmentTimeline>" << std::endl;
      r << "      </SegmentTemplate>" << std::endl;
 
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        if (it->second.codec == "AAC" || it->second.codec == "MP3" || it->second.codec == "AC3"){
          r << "      <Representation ";
          r << "id=\"" << it->first << "\" ";
          // (see RFC6381): sample description entry , ObjectTypeIndication [MP4RA, RFC], ObjectTypeIndication [MP4A ISO/IEC 14496-3:2009]
          if (it->second.codec == "AAC" ){
            r << "codecs=\"mp4a.40.2\" ";
          }else if (it->second.codec == "MP3" ){
            r << "codecs=\"mp4a.40.34\" ";
          }else if (it->second.codec == "AC3" ){
            r << "codecs=\"ec-3\" ";
          }
          r << "audioSamplingRate=\"" << it->second.rate << "\" ";
          r << "bandwidth=\"" << it->second.bps << "\">" << std::endl;
          r << "        <AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"" << it->second.channels << "\" />" << std::endl;
          r << "      </Representation>" << std::endl;
        }
      }
      r << "    </AdaptationSet>" << std::endl;
    }
    r << "  </Period>" << std::endl;
    r << "</MPD>" << std::endl;

    return r.str();
  }
  
  void OutDashMP4::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "DASHMP4";
    capa["desc"] = "Enables HTTP protocol progressive streaming.";
    capa["url_rel"] = "/dash/$/index.mpd";
    capa["url_prefix"] = "/dash/$/";
    capa["socket"] = "http_dash_mp4";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("AC3");
    capa["codecs"][0u][1u].append("MP3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "dash/video/mp4";
    capa["methods"][0u]["priority"] = 8ll;
    capa["methods"][0u]["nolive"] = 1;
  }
  
  /// Parses a "Range: " header, setting byteStart, byteEnd and seekPoint using data from metadata and tracks to do
  /// the calculations.
  /// On error, byteEnd is set to zero.
  void OutDashMP4::parseRange(std::string header, long long & byteStart, long long & byteEnd){
    int firstPos = header.find("=") + 1;
    byteStart = atoll(header.substr(firstPos, header.find("-", firstPos)).c_str());
    byteEnd = atoll(header.substr(header.find("-", firstPos) + 1).c_str());
    
    DEBUG_MSG(DLVL_DEVEL, "Range request: %lli-%lli (%s)", byteStart, byteEnd, header.c_str());
  }
  
  int OutDashMP4::getKeyFromRange(unsigned int tid, long long int byteStart){
    unsigned long long int currOffset = 0;
    for (int i = 0; i < myMeta.tracks[tid].keys.size(); i++){
      if (byteStart == currOffset){
        return i;
      }
      if (byteStart < currOffset && i > 0){
        return i - 1;
      }
      DEBUG_MSG(DLVL_DEVEL, "%lld > %llu", byteStart, currOffset);
    }
    return -1;
  }

  void OutDashMP4::initialize(){
    HTTPOutput::initialize();
    for (std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (!moovBoxes.count(it->first)){
        moovBoxes[it->first] = buildMoov(it->first);
      }
    }
  }
  
  void OutDashMP4::onHTTP(){
    initialize();
    if (myMeta.live){
      updateMeta();
    }
    std::string url = H.url;
    if (H.method == "OPTIONS"){
      H.Clean();
      H.SetHeader("Content-Type", "application/octet-stream");
      H.SetHeader("Cache-Control", "no-cache");
      H.setCORSHeaders();
      H.SetBody("");
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    if (url.find(".mpd") != std::string::npos){
      H.Clean();
      H.SetHeader("Content-Type", "application/xml");
      H.SetHeader("Cache-Control", "no-cache");
      H.setCORSHeaders();
      H.SetBody(buildManifest());
      H.SendResponse("200", "OK", myConn);
      DEVEL_MSG("Manifest sent");
    }else{
      long long int bench = Util::getMS();
      int pos = url.find("chunk_") + 6;//put our marker just after the _ beyond chunk
      int tid = atoi(url.substr(pos).c_str());
      DEBUG_MSG(DLVL_DEVEL, "Track %d requested", tid);
      
      H.Clean();
      H.SetHeader("Content-Type", "video/mp4");
      H.SetHeader("Cache-Control", "no-cache");
      H.setCORSHeaders();
      H.StartResponse(H, myConn);

      if (url.find("init.m4s") != std::string::npos){
        DEBUG_MSG(DLVL_DEVEL, "Handling init");
        buildFtyp(tid);
        H.Chunkify(moovBoxes[tid], myConn);
      }else{
        pos = url.find("_", pos + 1) + 1;
        int keyId = atoi(url.substr(pos).c_str());
        DEBUG_MSG(DLVL_DEVEL, "Searching for time %d", keyId);
        unsigned int keyNum = myMeta.tracks[tid].timeToKeynum(keyId);
        INFO_MSG("Detected key %d:%d for time %d", tid, keyNum, keyId);
        H.Chunkify("\000\000\000\030stypmsdh\000\000\000\000msdhmsix", 24, myConn);
        MP4::SIDX sidxBox;
        sidxBox.setReferenceID(1);
        sidxBox.setTimescale(1000);
        sidxBox.setEarliestPresentationTime(myMeta.tracks[tid].getKey(keyNum).getTime());
        sidxBox.setFirstOffset(0);
        MP4::sidxReference refItem;
        refItem.referenceType = false;
        if (myMeta.tracks[tid].getKey(keyNum).getLength()){
          refItem.subSegmentDuration = myMeta.tracks[tid].getKey(keyNum).getLength();
        }else{
          refItem.subSegmentDuration = myMeta.tracks[tid].lastms - myMeta.tracks[tid].getKey(keyNum).getTime();
        }
        refItem.sapStart = false;
        refItem.sapType = 0;
        refItem.sapDeltaTime = 0;
        sidxBox.setReference(refItem, 0);
        H.Chunkify(sidxBox.asBox(),sidxBox.boxedSize(), myConn);
        std::string tmp = buildMoof(tid, keyNum);
        H.Chunkify(tmp, myConn);
        buildMdat(tid, keyNum);
      }
      H.Chunkify("", 0, myConn);
      H.Clean();
      INFO_MSG("Done handling request, took %lld ms", Util::getMS() - bench);
      return;
    }
    H.Clean();
    parseData = false;
    wantRequest = true;
  }
    
  void OutDashMP4::sendNext(){}
  void OutDashMP4::sendHeader(){}
}
