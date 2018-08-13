#include "output_dash_mp4.h"
#include <mist/defines.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
#include <mist/mp4_dash.h>
#include <mist/checksum.h>
#include <mist/timing.h>
#include <iomanip>

namespace Mist{
  OutDashMP4::OutDashMP4(Socket::Connection & conn) : HTTPOutput(conn){
    uaDelay = 0;
    realTime = 0;
  }
  OutDashMP4::~OutDashMP4(){}
  
  std::string OutDashMP4::makeTime(uint64_t time){
    std::stringstream r;
    r << "PT";
    if (time >= 3600000){r << (time / 3600000) << "H";}
    if (time >= 60000){r << (time / 60000) % 60 << "M";}
    r << (time / 1000) % 60 << "." << std::setfill('0') << std::setw(3) << (time % 1000) << "S";
    return r.str();
  }
 
  /// Sends an empty moov box for the given track to the connected client, for following up with moof box(es).
  void OutDashMP4::sendMoov(uint32_t tid){
    DTSC::Track & Trk = myMeta.tracks[tid];

    MP4::MOOV moovBox;
    MP4::MVHD mvhdBox(0);
    mvhdBox.setTrackID(1);
    mvhdBox.setDuration(0xFFFFFFFF);
    moovBox.setContent(mvhdBox, 0);

    MP4::IODS iodsBox;
    if (Trk.type == "video"){
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
    MP4::TKHD tkhdBox(1, 0, Trk.width, Trk.height);
    tkhdBox.setFlags(3);
    if (Trk.type == "audio"){
      tkhdBox.setVolume(256);
      tkhdBox.setWidth(0);
      tkhdBox.setHeight(0);
    }
    tkhdBox.setDuration(0xFFFFFFFF);
    trakBox.setContent(tkhdBox, 0);
    
    MP4::MDIA mdiaBox;
    MP4::MDHD mdhdBox(0);
    mdhdBox.setLanguage(0x44);
    mdhdBox.setDuration(Trk.lastms);
    mdiaBox.setContent(mdhdBox, 0);
    
    if (Trk.type == "video"){
      MP4::HDLR hdlrBox(Trk.type,"VideoHandler");
      mdiaBox.setContent(hdlrBox, 1);
    }else{
      MP4::HDLR hdlrBox(Trk.type,"SoundHandler");
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
    
    if (Trk.codec == "H264"){
      MP4::AVC1 avc1Box;
      avc1Box.setWidth(Trk.width);
      avc1Box.setHeight(Trk.height);
      
      MP4::AVCC avccBox;
      avccBox.setPayload(Trk.init);
      avc1Box.setCLAP(avccBox);
      stsdBox.setEntry(avc1Box, 0);
    }
    if (Trk.codec == "HEVC"){
      MP4::HEV1 hev1Box;
      hev1Box.setWidth(Trk.width);
      hev1Box.setHeight(Trk.height);
      
      MP4::HVCC hvccBox;
      hvccBox.setPayload(Trk.init);
      hev1Box.setCLAP(hvccBox);
      stsdBox.setEntry(hev1Box, 0);
    }
    if (Trk.codec == "AAC" || Trk.codec == "MP3"){
      MP4::AudioSampleEntry ase;
      ase.setCodec("mp4a");
      ase.setDataReferenceIndex(1);
      ase.setSampleRate(Trk.rate);
      ase.setChannelCount(Trk.channels);
      ase.setSampleSize(Trk.size);
      MP4::ESDS esdsBox(Trk.init);
      ase.setCodecBox(esdsBox);
      stsdBox.setEntry(ase,0);
    }
    if (Trk.codec == "AC3"){
      ///\todo Note: this code is copied, note for muxing seperation
      MP4::AudioSampleEntry ase;
      ase.setCodec("ac-3");
      ase.setDataReferenceIndex(1);
      ase.setSampleRate(Trk.rate);
      ase.setChannelCount(Trk.channels);
      ase.setSampleSize(Trk.size);
      MP4::DAC3 dac3Box(Trk.rate, Trk.channels);
      ase.setCodecBox(dac3Box);
      stsdBox.setEntry(ase,0);
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
    
    if (Trk.type == "video"){
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
   
    H.Chunkify(moovBox.asBox(), moovBox.boxedSize(), myConn);
  }
    
  void OutDashMP4::sendMoof(uint32_t tid, uint32_t fragIndice){
    DTSC::Track & Trk = myMeta.tracks[tid];
    MP4::MOOF moofBox;
    MP4::MFHD mfhdBox;
    mfhdBox.setSequenceNumber(fragIndice + Trk.missedFrags);
    moofBox.setContent(mfhdBox, 0);
    MP4::TRAF trafBox;
    MP4::TFHD tfhdBox;
    tfhdBox.setTrackID(1);
    if (Trk.type == "audio"){
      tfhdBox.setFlags(MP4::tfhdSampleFlag);
      tfhdBox.setDefaultSampleFlags(MP4::isKeySample);
    }
    trafBox.setContent(tfhdBox, 0);
    MP4::TFDT tfdtBox;
    tfdtBox.setBaseMediaDecodeTime(Trk.getKey(Trk.fragments[fragIndice].getNumber()).getTime());
    trafBox.setContent(tfdtBox, 1);
    MP4::TRUN trunBox;

    if (Trk.type == "video"){
      uint32_t headSize = 0;
      if (Trk.codec == "H264"){
        MP4::AVCC avccBox;
        avccBox.setPayload(Trk.init);
        headSize = 14 + avccBox.getSPSLen() + avccBox.getPPSLen();
      }
      if (Trk.codec == "HEVC"){
        MP4::HVCC hvccBox;
        hvccBox.setPayload(myMeta.tracks[tid].init);
        std::deque<MP4::HVCCArrayEntry> content = hvccBox.getArrays();
        for (std::deque<MP4::HVCCArrayEntry>::iterator it = content.begin(); it != content.end(); it++){
          for (std::deque<std::string>::iterator it2 = it->nalUnits.begin(); it2 != it->nalUnits.end(); it2++){
            headSize += 4 + (*it2).size();
          }
        }
      }
      trunBox.setFlags(MP4::trundataOffset | MP4::trunsampleSize | MP4::trunsampleDuration | MP4::trunfirstSampleFlags | MP4::trunsampleOffsets);
      trunBox.setFirstSampleFlags(MP4::isKeySample);
      trunBox.setDataOffset(0);
      uint32_t j = 0;
      for (DTSC::PartIter parts(Trk, Trk.fragments[fragIndice]); parts; ++parts){
        MP4::trunSampleInformation trunEntry;
        trunEntry.sampleSize = parts->getSize();
        if (!j){
          trunEntry.sampleSize += headSize;
        }
        trunEntry.sampleDuration = parts->getDuration();
        trunEntry.sampleOffset = parts->getOffset();
        trunBox.setSampleInformation(trunEntry, j);
        ++j;
      }
      trunBox.setDataOffset(88 + (12 * j) + 8);
    }
    if (Trk.type == "audio"){
      trunBox.setFlags(MP4::trundataOffset | MP4::trunsampleSize | MP4::trunsampleDuration);
      trunBox.setDataOffset(0);
      uint32_t j = 0;
      for (DTSC::PartIter parts(Trk, Trk.fragments[fragIndice]); parts; ++parts){
        MP4::trunSampleInformation trunEntry;
        trunEntry.sampleSize = parts->getSize();
        trunEntry.sampleDuration = parts->getDuration();
        trunBox.setSampleInformation(trunEntry, j);
        ++j;
      }
      trunBox.setDataOffset(88 + (8 * j) + 8);
    }
    trafBox.setContent(trunBox, 2);
    moofBox.setContent(trafBox, 1);
    H.Chunkify(moofBox.asBox(), moofBox.boxedSize(), myConn);
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
  
  void OutDashMP4::sendMdat(uint32_t tid, uint32_t fragIndice){
    DTSC::Track & Trk = myMeta.tracks[tid];
    DTSC::Fragment & Frag = Trk.fragments[fragIndice];
    uint32_t size = 8 + Frag.getSize();
    if (Trk.codec == "H264"){
      MP4::AVCC avccBox;
      avccBox.setPayload(Trk.init);
      size += 14 + avccBox.getSPSLen() + avccBox.getPPSLen();
    }
    if (Trk.codec == "HEVC"){
      MP4::HVCC hvccBox;
      hvccBox.setPayload(Trk.init);
      std::deque<MP4::HVCCArrayEntry> content = hvccBox.getArrays();
      for (std::deque<MP4::HVCCArrayEntry>::iterator it = content.begin(); it != content.end(); it++){
        for (std::deque<std::string>::iterator it2 = it->nalUnits.begin(); it2 != it->nalUnits.end(); it2++){
          size += 4 + (*it2).size();
        }
      }
    }
    char mdatstr[8] ={0, 0, 0, 0, 'm', 'd', 'a', 't'};
    mdatstr[0] = (char)((size >> 24) & 0xFF);
    mdatstr[1] = (char)((size >> 16) & 0xFF);
    mdatstr[2] = (char)((size >> 8) & 0xFF);
    mdatstr[3] = (char)((size) & 0xFF);
    H.Chunkify(mdatstr, 8, myConn);
    std::string init;
    if (Trk.codec == "H264"){
      MP4::AVCC avccBox;
      avccBox.setPayload(Trk.init);
      init = buildNalUnit(2, "\011\340");
      H.Chunkify(init, myConn);//09E0
      init = buildNalUnit(avccBox.getSPSLen(), avccBox.getSPS());
      H.Chunkify(init, myConn);
      init = buildNalUnit(avccBox.getPPSLen(), avccBox.getPPS());
      H.Chunkify(init, myConn);
    }
    if (Trk.codec == "HEVC"){
      MP4::HVCC hvccBox;
      hvccBox.setPayload(Trk.init);
      std::deque<MP4::HVCCArrayEntry> content = hvccBox.getArrays();
      for (std::deque<MP4::HVCCArrayEntry>::iterator it = content.begin(); it != content.end(); it++){
        for (std::deque<std::string>::iterator it2 = it->nalUnits.begin(); it2 != it->nalUnits.end(); it2++){
          init = buildNalUnit((*it2).size(), (*it2).c_str());
          H.Chunkify(init, myConn);
        }
      }
    }
    //we pull these values first, because seek() destroys our Trk reference
    uint64_t startTime = Trk.getKey(Frag.getNumber()).getTime();
    targetTime = startTime + Frag.getDuration();
    HIGH_MSG("Starting playback from %llu to %llu", startTime, targetTime);
    wantRequest = false;
    parseData = true;
    //select only the tid track, and seek to the start time
    selectedTracks.clear();
    selectedTracks.insert(tid);
    seek(startTime);
  }

  void OutDashMP4::sendNext(){
    if (thisPacket.getTime() >= targetTime){
      HIGH_MSG("Finished playback to %llu", targetTime);
      wantRequest = true;
      parseData = false;
      H.Chunkify("", 0, myConn);
      H.Clean();
      return;
    }
    char *  data;
    unsigned int dataLen;
    thisPacket.getString("data", data, dataLen);
    H.Chunkify(data, dataLen, myConn);
  }

  std::string OutDashMP4::h264init(const std::string & initData){
    std::stringstream r;
    MP4::AVCC avccBox;
    avccBox.setPayload(initData);
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[1] << std::dec;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[2] << std::dec;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[3] << std::dec;
    return r.str();
  }

  std::string OutDashMP4::h265init(const std::string & initData){
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

  /// Examines Trk and adds playable fragments from it to r.
  void OutDashMP4::addSegmentTimeline(std::stringstream & r, DTSC::Track & Trk, bool live){
    std::deque<DTSC::Fragment>::iterator it = Trk.fragments.begin();
    bool first = true;
    //skip the first two fragments if live
    if (live && Trk.fragments.size() > 6){++(++it);}
    for (; it != Trk.fragments.end(); it++){
      uint64_t starttime = Trk.getKey(it->getNumber()).getTime();
      uint32_t duration = it->getDuration();
      if (!duration){
        if (live){continue;}//skip last fragment when live
        duration = Trk.lastms - starttime;
      }
      if (first){
        r << "          <S t=\"" << starttime << "\" d=\"" << duration << "\" />" << std::endl;
        first = false;
      }else{
        r << "          <S d=\"" << duration << "\" />" << std::endl;
      }
    }
  }

  /// Returns a string with the full XML DASH manifest MPD file.
  std::string OutDashMP4::buildManifest(){
    initialize();
    uint64_t lastVidTime = 0;
    uint64_t vidInitTrack = 0;
    uint64_t lastAudTime = 0;
    uint64_t audInitTrack = 0;
    uint64_t subInitTrack = 0;

    /// \TODO DASH pretends there is only one audio/video track, and then prints them all using the same timing information. This is obviously wrong if the tracks are not in sync.
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it ++){
      if ((it->second.codec == "H264" || it->second.codec == "HEVC") && it->second.lastms > lastVidTime){
        lastVidTime = it->second.lastms;
        vidInitTrack = it->first;
      }
      if ((it->second.codec == "AAC" || it->second.codec == "MP3" || it->second.codec == "AC3")&& it->second.lastms > lastAudTime){
        lastAudTime = it->second.lastms;
        audInitTrack = it->first;
      }
      if(it->second.codec == "subtitle"){
        subInitTrack = it->first;
      }
    }
    std::stringstream r;

    r << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl;
    r << "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 http://standards.iso.org/ittf/PubliclyAvailableStandards/MPEG-DASH_schema_files/DASH-MPD.xsd\" profiles=\"urn:mpeg:dash:profile:isoff-live:2011\" ";
    if (myMeta.vod){
      r << "type=\"static\" mediaPresentationDuration=\"" << makeTime(std::max(lastVidTime, lastAudTime)) << "\" minBufferTime=\"PT1.5S\" >" << std::endl;
    }else{
      r << "type=\"dynamic\" minimumUpdatePeriod=\"PT2.0S\" availabilityStartTime=\"" << Util::getUTCString(Util::epoch() - std::max(lastVidTime, lastAudTime)/1000) << "\" " << "timeShiftBufferDepth=\"" << makeTime(myMeta.tracks.begin()->second.lastms - myMeta.tracks.begin()->second.firstms) << "\" suggestedPresentationDelay=\"PT5.0S\" minBufferTime=\"PT2.0S\" >" << std::endl;
    }
    r << "  <ProgramInformation><Title>" << streamName << "</Title></ProgramInformation>" << std::endl;
    r << "  <Period ";
    if (myMeta.live){
      r << "id=\"0\" ";
    }
    r << "start=\"PT0S\">" << std::endl;
    if (vidInitTrack){
      DTSC::Track & trackRef = myMeta.tracks[vidInitTrack];
      r << "    <AdaptationSet id=\"0\" mimeType=\"video/mp4\" width=\"" << trackRef.width << "\" height=\"" << trackRef.height << "\" frameRate=\"" << trackRef.fpks / 1000 << "\" segmentAlignment=\"true\" startWithSAP=\"1\" subsegmentAlignment=\"true\" subsegmentStartsWithSAP=\"1\">" << std::endl;
      r << "      <SegmentTemplate timescale=\"1000\" media=\"chunk_$RepresentationID$_$Time$.m4s\" initialization=\"chunk_$RepresentationID$_init.m4s\">" << std::endl;
      r << "        <SegmentTimeline>" << std::endl;
      addSegmentTimeline(r, trackRef, myMeta.live);
      r << "        </SegmentTimeline>" << std::endl;
      r << "      </SegmentTemplate>" << std::endl;
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        if (it->second.codec == "H264"){
          r << "      <Representation ";
          r << "id=\"" << it->first << "\" ";
          r << "codecs=\"avc1." << h264init(it->second.init) << "\" ";
          //bandwidth is in bits per seconds, we have bytes, so times 8
          r << "bandwidth=\"" << (it->second.bps*8) << "\" ";
          r << "/>" << std::endl;
        }
        if (it->second.codec == "HEVC"){
          r << "      <Representation ";
          r << "id=\"" << it->first << "\" ";
          r << "codecs=\"hev1." << h265init(it->second.init) << "\" ";
          //bandwidth is in bits per seconds, we have bytes, so times 8
          r << "bandwidth=\"" << (it->second.bps*8) << "\" ";
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
      addSegmentTimeline(r, trackRef, myMeta.live);
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
          //bandwidth is in bits per seconds, we have bytes, so times 8
          r << "bandwidth=\"" << (it->second.bps*8) << "\">" << std::endl;
          r << "        <AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"" << it->second.channels << "\" />" << std::endl;
          r << "      </Representation>" << std::endl;
        }
      }
      r << "    </AdaptationSet>" << std::endl;
    }

    if(subInitTrack){
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        if(it->second.codec == "subtitle"){
          subInitTrack = it->first;
          std::string lang = (it->second.lang == "" ? "unknown" : it->second.lang);
          r << "<AdaptationSet group=\"3\" mimeType=\"text/vtt\" lang=\"" << lang <<  "\">";
          r << " <Representation id=\"caption_en"<< it->first << "\" bandwidth=\"256\">";
          r <<   " <BaseURL>../../" << streamName << ".vtt?track=" << it->first << "</BaseURL>";
          r << " </Representation></AdaptationSet>" << std::endl;
        }
      }
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
    capa["codecs"][0u][2u].append("subtitle");

    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "dash/video/mp4";

    //MP3 does not work in browsers
    capa["exceptions"]["codec:MP3"] = JSON::fromString("[[\"blacklist\",[\"Mozilla/\"]]]");
   capa["methods"][0u]["priority"] = 8ll;

    cfg->addOption("nonchunked", JSON::fromString("{\"short\":\"C\",\"long\":\"nonchunked\",\"help\":\"Do not send chunked, but buffer whole segments.\"}"));
    capa["optional"]["nonchunked"]["name"] = "Send whole segments";
    capa["optional"]["nonchunked"]["help"] = "Disables chunked transfer encoding, forcing per-segment buffering. Reduces performance significantly, but increases compatibility somewhat.";
    capa["optional"]["nonchunked"]["option"] = "--nonchunked";
  }
  
  void OutDashMP4::onHTTP(){
    std::string method = H.method;
    
    initialize();
    if (myMeta.live){
      updateMeta();
    }
    std::string url = H.url;
    // Send a manifest for any URL with .mpd in the path
    if (url.find(".mpd") != std::string::npos){
      H.Clean();
      H.SetHeader("Content-Type", "application/dash+xml");
      H.SetHeader("Cache-Control", "no-cache");
      H.setCORSHeaders();
      if(method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      H.SetBody(buildManifest());
      H.SendResponse("200", "OK", myConn);
      DEVEL_MSG("Manifest sent");
      H.Clean();
      return;
    }

    //Not a manifest - either an init segment or data segment
    size_t pos = url.find("chunk_") + 6;//find the track ID position
    uint32_t tid = atoi(url.substr(pos).c_str());
    if (!myMeta.tracks.count(tid)){
      H.Clean();
      H.SendResponse("404", "Track not found", myConn);
      H.Clean();
      return;
    }
    DTSC::Track & Trk = myMeta.tracks[tid];
    H.Clean();
    H.SetHeader("Content-Type", "video/mp4");
    H.SetHeader("Cache-Control", "no-cache");
    H.setCORSHeaders();
    if(method == "OPTIONS" || method == "HEAD"){
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    H.StartResponse(H, myConn, config->getBool("nonchunked"));

    if (url.find("init.m4s") != std::string::npos){
      //init segment
      if (Trk.type == "video"){
        H.Chunkify("\000\000\000\040ftypisom\000\000\000\000isomavc1mp42dash", 32, myConn);
      }else{
        H.Chunkify("\000\000\000\040ftypisom\000\000\000\000isomM4A mp42dash", 32, myConn);
      }
      sendMoov(tid);
      H.Chunkify("", 0, myConn);
      H.Clean();
      return;
    }

    //data segment
    pos = url.find("_", pos + 1) + 1;
    uint64_t timeStamp = atoll(url.substr(pos).c_str());
    uint32_t fragIndice = Trk.timeToFragnum(timeStamp);
    uint32_t fragNum = Trk.fragments[fragIndice].getNumber();
    HIGH_MSG("Getting T%llu for track %lu, indice %lu, number %lu", timeStamp, tid, fragIndice, fragNum);
    H.Chunkify("\000\000\000\030stypmsdh\000\000\000\000msdhmsix", 24, myConn);
    MP4::SIDX sidxBox;
    sidxBox.setReferenceID(1);
    sidxBox.setTimescale(1000);
    sidxBox.setEarliestPresentationTime(Trk.getKey(fragNum).getTime());
    sidxBox.setFirstOffset(0);
    MP4::sidxReference refItem;
    refItem.referenceType = false;
    if (Trk.fragments[fragIndice].getDuration()){
      refItem.subSegmentDuration = Trk.fragments[fragIndice].getDuration();
    }else{
      refItem.subSegmentDuration = Trk.lastms - Trk.getKey(fragNum).getTime();
    }
    refItem.sapStart = false;
    refItem.sapType = 0;
    refItem.sapDeltaTime = 0;
    sidxBox.setReference(refItem, 0);
    H.Chunkify(sidxBox.asBox(), sidxBox.boxedSize(), myConn);
    sendMoof(tid, fragIndice);
    sendMdat(tid, fragIndice);
  }
    
}

