#include "output_hds.h"
#include <mist/amf.h>
#include <mist/mp4_adobe.h>
#include <mist/stream.h>
#include <unistd.h>

namespace Mist{

  void OutHDS::getTracks(){
    videoTracks.clear();
    audioTrack = INVALID_TRACK_ID;
    JSON::Value &vidCapa = capa["codecs"][0u][0u];
    JSON::Value &audCapa = capa["codecs"][0u][1u];
    std::set<size_t> validTracks = M.getValidTracks();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
      jsonForEach(vidCapa, itb){
        if (M.getCodec(*it) == itb->asStringRef()){
          videoTracks.insert(*it);
          break;
        }
      }
      if (audioTrack == INVALID_TRACK_ID){
        jsonForEach(audCapa, itb){
          if (M.getCodec(*it) == itb->asStringRef()){
            audioTrack = *it;
            break;
          }
        }
      }
    }
  }

  ///\brief Builds a bootstrap for use in HTTP Dynamic streaming.
  ///\param tid The track this bootstrap is generated for.
  ///\return The generated bootstrap.
  std::string OutHDS::dynamicBootstrap(size_t idx){
    DTSC::Fragments fragments(M.fragments(idx));
    DTSC::Keys keys(M.keys(idx));
    std::string empty;

    MP4::ASRT asrt;
    asrt.setUpdate(false);
    asrt.setVersion(1);
    // asrt.setQualityEntry(empty, 0);
    if (M.getLive()){
      asrt.setSegmentRun(1, 4294967295ul, 0);
    }else{
      asrt.setSegmentRun(1, fragments.getValidCount(), 0);
    }

    MP4::AFRT afrt;
    afrt.setUpdate(false);
    afrt.setVersion(1);
    afrt.setTimeScale(1000);
    // afrt.setQualityEntry(empty, 0);
    MP4::afrt_runtable afrtrun;
    size_t i = 0;
    size_t j = 0;
    uint64_t firstTime = keys.getTime(fragments.getFirstKey(fragments.getFirstValid()));
    for (size_t fragIdx = fragments.getFirstValid() + 1; fragIdx < fragments.getEndValid(); ++fragIdx){
      if (M.getVod() || fragments.getDuration(fragIdx) > 0){
        afrtrun.firstFragment = M.getMissedFragments(idx) + j + 1;
        afrtrun.firstTimestamp = keys.getTime(fragments.getFirstKey(fragIdx)) - firstTime;
        if (fragments.getDuration(fragIdx) > 0){
          afrtrun.duration = fragments.getDuration(fragIdx);
        }else{
          afrtrun.duration = M.getLastms(idx) - afrtrun.firstTimestamp;
        }
        afrt.setFragmentRun(afrtrun, i++);
      }
      ++j;
    }

    MP4::ABST abst;
    abst.setVersion(1);
    abst.setBootstrapinfoVersion(1);
    abst.setProfile(0);
    abst.setUpdate(false);
    abst.setTimeScale(1000);
    abst.setLive(M.getLive());
    abst.setCurrentMediaTime(M.getLastms(idx));
    abst.setSmpteTimeCodeOffset(0);
    abst.setMovieIdentifier(streamName);
    abst.setSegmentRunTable(asrt, 0);
    abst.setFragmentRunTable(afrt, 0);

    VERYHIGH_MSG("Sending bootstrap: %s", abst.toPrettyString(0).c_str());
    return std::string(abst.asBox(), abst.boxedSize());
  }

  ///\brief Builds an index file for HTTP Dynamic streaming.
  ///\return The index file for HTTP Dynamic Streaming.
  std::string OutHDS::dynamicIndex(){
    getTracks();
    std::stringstream Result;
    Result << "<?xml version=\"1.0\" encoding=\"utf-8\"?>" << std::endl;
    Result << "  <manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">" << std::endl;
    Result << "  <id>" << streamName << "</id>" << std::endl;
    Result << "  <mimeType>video/mp4</mimeType>" << std::endl;
    Result << "  <deliveryType>streaming</deliveryType>" << std::endl;
    if (M.getVod()){
      Result << "  <duration>" << M.getLastms(videoTracks.size() ? *videoTracks.begin() : audioTrack) / 1000
             << ".000</duration>" << std::endl;
      Result << "  <streamType>recorded</streamType>" << std::endl;
    }else{
      Result << "  <duration>0.00</duration>" << std::endl;
      Result << "  <streamType>live</streamType>" << std::endl;
    }
    for (std::set<size_t>::iterator it = videoTracks.begin(); it != videoTracks.end(); it++){
      Result << "  <bootstrapInfo "
                "profile=\"named\" "
                "id=\"boot"
             << *it
             << "\" "
                "url=\""
             << *it
             << ".abst\">"
                "</bootstrapInfo>"
             << std::endl;
      Result << "  <media "
                "url=\""
             << *it
             << "-\" "
                // bitrate in kbit/s, we have bps so divide by 128
                "bitrate=\""
             << M.getBps(*it) / 128
             << "\" "
                "bootstrapInfoId=\"boot"
             << *it
             << "\" "
                "width=\""
             << M.getWidth(*it)
             << "\" "
                "height=\""
             << M.getHeight(*it) << "\">" << std::endl;
      Result << "    <metadata>AgAKb25NZXRhRGF0YQMAAAk=</metadata>" << std::endl;
      Result << "  </media>" << std::endl;
    }
    Result << "</manifest>" << std::endl;
    HIGH_MSG("Sending manifest: %s", Result.str().c_str());
    return Result.str();
  }// BuildManifest

  OutHDS::OutHDS(Socket::Connection &conn) : HTTPOutput(conn){
    uaDelay = 0;
    realTime = 0;
    audioTrack = INVALID_TRACK_ID;
    playUntil = 0;
  }

  OutHDS::~OutHDS(){}

  void OutHDS::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "HDS";
    capa["friendly"] = "Flash segmented over HTTP (HDS)";
    capa["desc"] = "Segmented streaming in Adobe/Flash (FLV-based) format over HTTP ( = HTTP "
                   "Dynamic Streaming)";
    capa["url_rel"] = "/dynamic/$/manifest.f4m";
    capa["url_prefix"] = "/dynamic/$/";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("H263");
    capa["codecs"][0u][0u].append("VP6");
    capa["codecs"][0u][0u].append("VP6Alpha");
    capa["codecs"][0u][0u].append("ScreenVideo2");
    capa["codecs"][0u][0u].append("ScreenVideo1");
    capa["codecs"][0u][0u].append("JPEG");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("Speex");
    capa["codecs"][0u][1u].append("Nellymoser");
    capa["codecs"][0u][1u].append("PCM");
    capa["codecs"][0u][1u].append("ADPCM");
    capa["codecs"][0u][1u].append("ALAW");
    capa["codecs"][0u][1u].append("ULAW");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "flash/11";
    capa["methods"][0u]["hrn"] = "Flash Dynamic (HDS)";
    capa["methods"][0u]["priority"] = 6;
    capa["methods"][0u]["player_url"] = "/flashplayer.swf";
  }

  void OutHDS::sendNext(){
    if (thisPacket.getTime() >= playUntil){
      VERYHIGH_MSG("Done sending fragment (%" PRIu64 " >= %" PRIu64 ")", thisPacket.getTime(), playUntil);
      stop();
      wantRequest = true;
      H.Chunkify("", 0, myConn);
      return;
    }
    tag.DTSCLoader(thisPacket, M, thisIdx);
    if (M.getCodec(thisIdx) == "PCM" && M.getSize(thisIdx) == 16){
      char *ptr = tag.getData();
      uint32_t ptrSize = tag.getDataLen();
      for (uint32_t i = 0; i < ptrSize; i += 2){
        char tmpchar = ptr[i];
        ptr[i] = ptr[i + 1];
        ptr[i + 1] = tmpchar;
      }
    }
    if (tag.len){H.Chunkify(tag.data, tag.len, myConn);}
  }

  void OutHDS::onHTTP(){
    std::string method = H.method;

    if (H.url.find(".abst") != std::string::npos){
      initialize();
      std::string streamID = H.url.substr(streamName.size() + 10);
      streamID = streamID.substr(0, streamID.find(".abst"));
      H.Clean();
      H.SetHeader("Content-Type", "binary/octet");
      H.SetHeader("Cache-Control", "no-cache");
      H.setCORSHeaders();
      if (method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      H.SetBody(dynamicBootstrap(atoll(streamID.c_str())));
      H.SendResponse("200", "OK", myConn);
      H.Clean(); // clean for any possible next requests
      return;
    }
    if (H.url.find("f4m") == std::string::npos){
      initialize();
      std::string tmp_qual = H.url.substr(H.url.find("/", 10) + 1);
      size_t idx = atoi(tmp_qual.substr(0, tmp_qual.find("Seg") - 1).c_str());
      if (idx == INVALID_TRACK_ID){FAIL_MSG("Requested fragment for invalid track id");}
      int temp;
      temp = H.url.find("Seg") + 3;
      temp = H.url.find("Frag") + 4;
      size_t fragIdx = atoi(H.url.substr(temp).c_str()) - 1;
      MEDIUM_MSG("Video track %zu, fragment %zu", idx, fragIdx);
      if (audioTrack == INVALID_TRACK_ID){getTracks();}
      uint64_t mstime = 0;
      uint64_t mslen = 0;
      if (fragIdx < M.getMissedFragments(idx)){
        H.Clean();
        H.setCORSHeaders();
        H.SetBody("The requested fragment is no longer kept in memory on the server and cannot be "
                  "served.\n");
        H.SendResponse("412", "Fragment out of range", myConn);
        H.Clean(); // clean for any possible next requests
        FAIL_MSG("Fragment %zu  too old", fragIdx);
        return;
      }
      // delay if we don't have the next fragment available yet
      unsigned int timeout = 0;
      DTSC::Fragments fragments(M.fragments(idx));
      DTSC::Keys keys(M.keys(idx));
      while (myConn && fragIdx >= fragments.getEndValid() - 1){
        // time out after 21 seconds
        if (++timeout > 42){
          onFail("Timeout triggered", true);
          break;
        }
        Util::wait(500);
      }
      mstime = keys.getTime(fragments.getFirstKey(fragIdx));
      mslen = fragments.getDuration(fragIdx);
      VERYHIGH_MSG("Playing from %" PRIu64 " for %" PRIu64 " ms", mstime, mslen);

      userSelect.clear();
      userSelect[idx].reload(streamName, idx);
      if (audioTrack != INVALID_TRACK_ID){userSelect[audioTrack].reload(streamName, audioTrack);}
      seek(mstime);
      playUntil = mstime + mslen;

      H.Clean();
      H.SetHeader("Content-Type", "video/mp4");
      H.setCORSHeaders();
      if (method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      H.StartResponse(H, myConn);
      // send the bootstrap
      H.Chunkify(dynamicBootstrap(idx), myConn);
      // send a zero-size mdat, meaning it stretches until end of file.
      H.Chunkify("\000\000\000\000mdat", 8, myConn);
      // send init data, if needed.
      if (audioTrack != INVALID_TRACK_ID && M.getInit(audioTrack) != ""){
        if (tag.DTSCAudioInit(meta.getCodec(audioTrack), meta.getRate(audioTrack), meta.getSize(audioTrack), meta.getChannels(audioTrack), meta.getInit(audioTrack))){
          tag.tagTime(mstime);
          H.Chunkify(tag.data, tag.len, myConn);
        }
      }
      if (idx != INVALID_TRACK_ID){
        if (tag.DTSCVideoInit(meta, idx)){
          tag.tagTime(mstime);
          H.Chunkify(tag.data, tag.len, myConn);
        }
      }
      parseData = true;
      wantRequest = false;
    }else{
      initialize();
      H.Clean();
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Cache-Control", "no-cache");
      H.setCORSHeaders();
      if (method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      H.SetBody(dynamicIndex());
      H.SendResponse("200", "OK", myConn);
    }
  }
}// namespace Mist
