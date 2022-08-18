/// \file analyser_rtmp.cpp
/// Debugging tool for RTMP data.

#include "analyser_rtmp.h"
#include <mist/bitfields.h>

void AnalyserRTMP::init(Util::Config &conf){
  Analyser::init(conf);
  JSON::Value opt;
  opt["long"] = "reconstruct";
  opt["short"] = "R";
  opt["arg"] = "string";
  opt["default"] = "";
  opt["help"] = "Reconstruct FLV file from RTMP stream to the given filename";
  conf.addOption("reconstruct", opt);
  opt.null();
}

/// Checks if standard input is still valid.
bool AnalyserRTMP::isOpen(){
  return (*isActive) && (std::cin.good() || strbuf.size());
}

AnalyserRTMP::AnalyserRTMP(Util::Config &conf) : Analyser(conf){
  if (conf.getString("reconstruct") != ""){
    reconstruct.open(conf.getString("reconstruct").c_str());
    if (reconstruct.good()){
      reconstruct.write(FLV::Header, 13);
      WARN_MSG("Will reconstruct to %s", conf.getString("reconstruct").c_str());
    }
  }
}

bool AnalyserRTMP::open(const std::string &filename){
  if (!Analyser::open(filename)){return false;}
  // Skip the 3073 byte handshake - there is no (truly) useful data in this.
  MEDIUM_MSG("Skipping handshake...");
  std::string inbuffer;
  inbuffer.reserve(3073);
  while (std::cin.good() && inbuffer.size() < 3073){inbuffer += std::cin.get();}
  RTMPStream::rec_cnt += 3073;
  inbuffer.erase(0, 3073); // strip the handshake part
  MEDIUM_MSG("Handshake skipped");
  return true;
}

bool AnalyserRTMP::parsePacket(){
  // While we can't parse a packet,
  while (!next.Parse(strbuf)){
    // fill our internal buffer "strbuf" in (up to) 1024 byte chunks
    if (!std::cin.good()){
      strbuf.clear();
      return false;
    }
    size_t charCount = 0;
    std::string tmpbuffer;
    tmpbuffer.reserve(1024);
    while (std::cin.good() && charCount < 1024){
      char newchar = std::cin.get();
      if (std::cin.good()){
        tmpbuffer += newchar;
        ++read_in;
        ++charCount;
      }
    }
    strbuf.append(tmpbuffer);
  }

  // We now know for sure that we've parsed a packet
  DETAIL_HI("Chunk info: [%#2X] CS ID %u, timestamp %" PRIu64 ", len %u, type ID %u, Stream ID %u",
            next.headertype, next.cs_id, next.timestamp, next.len, next.msg_type_id, next.msg_stream_id);
  switch (next.msg_type_id){
  case 0: // does not exist
    DETAIL_LOW("Error chunk @ %zu - CS%u, T%" PRIu64 ", L%i, LL%i, MID%i", read_in - strbuf.size(),
               next.cs_id, next.timestamp, next.real_len, next.len_left, next.msg_stream_id);
    return 0;
    break; // happens when connection breaks unexpectedly
  case 1:  // set chunk size
    RTMPStream::chunk_rec_max = ntohl(*(int *)next.data.c_str());
    DETAIL_MED("CTRL: Set chunk size: %zu", RTMPStream::chunk_rec_max);
    break;
  case 2: // abort message - we ignore this one
    DETAIL_MED("CTRL: Abort message: %" PRIu32, Bit::btohl(next.data.data()));
    // 4 bytes of stream id to drop
    break;
  case 3: // ack
    RTMPStream::snd_window_at = Bit::btohl(next.data.data());
    DETAIL_MED("CTRL: Acknowledgement: %zu", RTMPStream::snd_window_at);
    break;
  case 4:{
    int16_t ucmtype = Bit::btohs(next.data.data());
    switch (ucmtype){
    case 0:
      DETAIL_MED("CTRL: User control message: stream begin %" PRIu32, Bit::btohl(next.data.data() + 2));
      break;
    case 1:
      DETAIL_MED("CTRL: User control message: stream EOF %" PRIu32, Bit::btohl(next.data.data() + 2));
      break;
    case 2:
      DETAIL_MED("CTRL: User control message: stream dry %" PRIu32, Bit::btohl(next.data.data() + 2));
      break;
    case 3:
      DETAIL_MED("CTRL: User control message: setbufferlen %" PRIu32, Bit::btohl(next.data.data() + 2));
      break;
    case 4:
      DETAIL_MED("CTRL: User control message: streamisrecorded %" PRIu32, Bit::btohl(next.data.data() + 2));
      break;
    case 6:
      DETAIL_MED("CTRL: User control message: pingrequest %" PRIu32, Bit::btohl(next.data.data() + 2));
      break;
    case 7:
      DETAIL_MED("CTRL: User control message: pingresponse %" PRIu32, Bit::btohl(next.data.data() + 2));
      break;
    case 31:
    case 32:
      // don't know, but not interes ting anyway
      break;
    default:
      DETAIL_LOW("CTRL: User control message: UNKNOWN %" PRId16 " - %" PRIu32, ucmtype,
                 Bit::btohl(next.data.data() + 2));
      break;
    }
  }break;
  case 5: // window size of other end
    RTMPStream::rec_window_size = Bit::btohl(next.data.data());
    RTMPStream::rec_window_at = RTMPStream::rec_cnt;
    DETAIL_MED("CTRL: Window size: %zu", RTMPStream::rec_window_size);
    break;
  case 6:
    RTMPStream::snd_window_size = Bit::btohl(next.data.data());
    // 4 bytes window size, 1 byte limit type (ignored)
    DETAIL_MED("CTRL: Set peer bandwidth: %zu", RTMPStream::snd_window_size);
    break;
  case 8:
  case 9:
    if (detail >= 4 || reconstruct.good() || validate){
      F.ChunkLoader(next);
      mediaTime = F.tagTime();
      DETAIL_VHI("[%" PRIu64 "+%" PRId64 "] %s", F.tagTime(), F.offset(), F.tagType().c_str());
      if (reconstruct.good()){reconstruct.write(F.data, F.len);}
    }
    break;
  case 15: DETAIL_MED("Received AFM3 data message"); break;
  case 16: DETAIL_MED("Received AFM3 shared object"); break;
  case 17:{
    DETAIL_MED("Received AFM3 command message:");
    char soort = next.data[0];
    next.data = next.data.substr(1);
    if (soort == 0){
      amfdata = AMF::parse(next.data);
      DETAIL_MED("%s", amfdata.Print().c_str());
    }else{
      amf3data = AMF::parse3(next.data);
      DETAIL_MED("%s", amf3data.Print().c_str());
    }
  }break;
  case 18:{
    DETAIL_MED("Received AFM0 data message (metadata):");
    amfdata = AMF::parse(next.data);
    DETAIL_MED("%s", amfdata.Print().c_str());
    if (reconstruct.good()){
      F.ChunkLoader(next);
      reconstruct.write(F.data, F.len);
    }
  }break;
  case 19: DETAIL_MED("Received AFM0 shared object"); break;
  case 20:{// AMF0 command message
    DETAIL_MED("Received AFM0 command message:");
    amfdata = AMF::parse(next.data);
    DETAIL_MED("%s", amfdata.Print().c_str());
  }break;
  case 22:
    if (reconstruct.good()){reconstruct << next.data;}
    break;
  default:
    FAIL_MSG(
        "Unknown chunk received! Probably protocol corruption, stopping parsing of incoming data.");
    return false;
    break;
  }// switch for type of chunk
  return true;
}
