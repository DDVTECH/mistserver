/// \file analyser_rtmp.cpp
/// Debugging tool for RTMP data.

#include "analyser_rtmp.h"

void AnalyserRTMP::init(Util::Config &conf){
  Analyser::init(conf);
  JSON::Value opt;
  opt["long"] = "reconstruct";
  opt["short"] = "R";
  opt["arg"] = "string";
  opt["default"] = "";
  opt["help"] = "Reconstruct FLV file from a RTMP stream to a file.";
  conf.addOption("reconstruct", opt);
  opt.null();
}

AnalyserRTMP::AnalyserRTMP(Util::Config &conf) : Analyser(conf){
  // Opens target location to where we want to save the stream 
  if (conf.getString("reconstruct") != ""){
    reconstruct.open(conf.getString("reconstruct").c_str());
    if (reconstruct.good()){
      reconstruct.write(FLV::Header, 13);
      INFO_MSG("Will reconstruct to %s", conf.getString("reconstruct").c_str());
    }
  }
}

// Override default function (check tcpCon rather than isEOF)
bool AnalyserRTMP::isOpen(){
  if (isFile){
    return (*isActive) && !uri.isEOF();
  }
  else{
    return (*isActive) && tcpCon;
  }
}

// Opens a connection to the URL. Calls doHandshake and startStream
//  to make the stream ready for parsePacket
bool AnalyserRTMP::open(const std::string & url){
  // Just in case
  tcpCon.close();
  tcpCon.Received().clear();

  // We will assume a link to a file if the URL does not start with RTMP
  if (url.substr(0, 4) != "rtmp"){
    isFile = true;
    MEDIUM_MSG("Trying to read RTMP stream data from: %s", url.c_str());
    if (!Analyser::open(url)){return false;}

    // Handshake (3073 B) contains no meaningful data 
    HIGH_MSG("Skipping handshake...");
    std::string inBuffer;
    const size_t bytesNeeded = 3073;

    while (inBuffer.size() < bytesNeeded) {
      if (uri.isEOF()) {
        FAIL_MSG("End of file");
        return false;
      }
      inBuffer.append(removeFromBufferBlocking(bytesNeeded));
    }
    RTMPStream::rec_cnt += bytesNeeded;
    inBuffer.erase(0, bytesNeeded);
  }else{
    isFile = false;
    HIGH_MSG("Opening connection with %s", url.c_str());
    pushUrl = HTTP::URL(url);
    tcpCon.open(pushUrl.host, pushUrl.getPort(), false);

    if (!tcpCon){
      FAIL_MSG("Could not connect to %s:%d!", pushUrl.host.c_str(), pushUrl.getPort());
      return false;
    }

    // Client should init the handshake and parse the response
    if (!doHandshake()){
      FAIL_MSG("Failed to perform RTMP handshake. Aborting");
      return false;
    }

    // Client should send connect, createstream, (ping omitted) and play command in that order
    requestStream();
  }
  return true;
}

// Waits until the buffer contains bytesNeeded amount of bytes, then removes this from buffer and returns it
std::string AnalyserRTMP::removeFromBufferBlocking(size_t bytesNeeded){
  HIGH_MSG("Getting %lu B from buffer", bytesNeeded);
  // Use urireader to get RTMP dump from file or URL
  if (isFile){
    char* inBuffer;
    size_t dataLen = 0;
    size_t bytesLeft = bytesNeeded;

    while (bytesLeft > 0) {
      // If EOF, return whatever we have read
      if (uri.isEOF()) {
        bytesNeeded -= bytesLeft;
        return std::string(inBuffer, bytesNeeded);
      }
      // Else keep reading until EOF or there is enough data
      uri.readSome(inBuffer, dataLen, bytesLeft);
      bytesLeft -= dataLen;
      RTMPStream::rec_cnt += dataLen;
    }
    return std::string(inBuffer, bytesNeeded);
  // Otherwise use TCPCon to get latest data
  }else{
    tcpCon.setBlocking(true);
    while (!tcpCon.Received().available(bytesNeeded) && tcpCon.connected()){
      tcpCon.spool();
    }
    tcpCon.setBlocking(false);

    if (!tcpCon.connected()){
      return "";
    }

    RTMPStream::rec_cnt += bytesNeeded;
    return tcpCon.Received().remove(bytesNeeded);
  }
}

// Performs the RTMP handshake before a stream gets initiated
//  Return  True if handshake was a success
//          False if it is invalid or the connection closed
bool AnalyserRTMP::doHandshake(){
  // Will contain (part of) the handshake we are sending
  //  this will initially be C0 and C1, to be replaced by C2
  //  when we have confirmed S0 an S1
  std::string handshake_out;
  // Used to fill handshake_out more efficiently
  uint8_t *outBufIt;
  // Will contain (part of) the handshake we are receiving
  std::string handshake_in;
  // Copy of C1 to verify S2
  std::string tmpStr;

  // C0/S0 are 1 byte, C1/S2 are 1536. We send/receive both at the same time
  size_t bytesNeeded = 1537;

  // Build C0 and C1
  handshake_out.resize(bytesNeeded);
  outBufIt = (uint8_t *)handshake_out.data();
  // C0 = 1 byte of version number
  outBufIt[0] = (char)0x03;
  // C1 startsWith 8 bytes of nothingness
  for (int i = 1; i < 9; i++){
    outBufIt[i] = (char)0x0;
  }
  // rest of C1 is random
  for (int i = 9; i < bytesNeeded; i++){
    outBufIt[i] = rand();
  }
  // Save C1 -> compare with S2 later
  tmpStr.append(handshake_out.substr(1));

  // Client initiates handshake by sending C0 and C1
  tcpCon.SendNow(handshake_out);
  RTMPStream::snd_cnt += bytesNeeded;

  // Server responds with (at least) S0 and S1, which are 1 + 1536 bytes
  handshake_in.append(removeFromBufferBlocking(bytesNeeded));

  // Check S0
  if (handshake_in[0] != handshake_out[0]){
    FAIL_MSG("Version between RTMP server and client does not match (C0 is %x and S0 is %x)", handshake_out[0], handshake_in[0]);
    return false;
  }
  
  // Size of C2/S2
  bytesNeeded = 1536;
  // We checked S0 so we can remove it
  handshake_in.erase(0, 1);
  // Build C2 (which is a copy of S1 except for bytes 5-8)
  handshake_out.clear();
  handshake_out.append(handshake_in);
  // NOTE that we should insert current timestamp here to bytes 5-8 (but not that important)

  // Send C2
  tcpCon.SendNow(handshake_out);
  RTMPStream::snd_cnt += bytesNeeded;

  // Receive S2
  handshake_in.clear();
  handshake_in.append(removeFromBufferBlocking(bytesNeeded));

  // Leave out the first 8 bytes, (4B of 0, 4B of timestamp the packet was read)
  // since we only want to compare the random bytes we sent earlier
  handshake_in = handshake_in.substr(8);
  tmpStr = tmpStr.substr(8);

  if (handshake_in.compare(tmpStr) != 0){
    WARN_MSG("Received packet S2 does not match sent packet C1!");
  }

  DONTEVEN_MSG("Handshake success");
  return true;
}

// Send AMF commands to initiate stream
void AnalyserRTMP::requestStream(){
  // URL path has app/stream -> extract app
  std::string app = Encodings::URL::encode(pushUrl.path, "/:=@[]");
  std::string targetStream = "";
  size_t slash = app.find('/');
  if (slash != std::string::npos){
    targetStream = app.substr(slash);
    app = app.substr(0, slash);
  }

  // Build URL with protocol, host and port (without path)
  std::string pushHost = "rtmp://" + pushUrl.host + "/";
  if (pushUrl.getPort() != 1935){
    pushHost = "rtmp://" + pushUrl.host + ":" + JSON::Value(pushUrl.getPort()).asString() + "/";
  }

  HIGH_MSG("Sending with app=%s, pushHost=%s", app.c_str(), pushHost.c_str());

  // Client starts with CONNECT request
  AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
  amfReply.addContent(AMF::Object("", "connect"));          // cmd: connect
  amfReply.addContent(AMF::Object("", 1.0));                // transaction ID: 1.0 for all
  amfReply.addContent(AMF::Object(""));                     // command object: name-value pairs
  amfReply.getContentP(2)->addContent(AMF::Object("app", app));
  amfReply.getContentP(2)->addContent(AMF::Object("flashVer", "FMLE/3.0 (compatible; " APPNAME ")"));
  amfReply.getContentP(2)->addContent(AMF::Object("type", "nonprivate"));
  // tcURL should contain entire server url, including port, app (NOTE might need streamname as well for some implementations?)
  amfReply.getContentP(2)->addContent(AMF::Object("tcUrl", pushHost + app));
  // Omitted: optional user arguments for any additional info
  tcpCon.SendNow(RTMPStream::SendChunk(3, 20, 0, amfReply.Pack()));

  RTMPStream::chunk_snd_max = 65536;                                 // 64KiB
  tcpCon.SendNow(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); // send chunk size max (msg 1)

  // Followed by createStream
  amfReply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
  amfReply.addContent(AMF::Object("", "createStream"));      // cmd: createStream
  amfReply.addContent(AMF::Object("", 1.0));                 // tID 1.0 for all
  amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // command object: null for now
  tcpCon.SendNow(RTMPStream::SendChunk(3, 20, 0, amfReply.Pack()));

  // Followed by a play invocation with the actual stream name
  amfReply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
  amfReply.addContent(AMF::Object("", "play"));               // Command name: play
  amfReply.addContent(AMF::Object("", 1.0));                  // tID: we use 1.0 for all
  amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL));  // command info: null obj
  amfReply.addContent(AMF::Object("", targetStream));         // streamname (precede with type eg mp3:sample)
  // Omitted: optional start parameter: startTime in seconds
  // Omitted: optional duration parameter
  tcpCon.SendNow(RTMPStream::SendChunk(3, 20, 0, amfReply.Pack()));
}

// Parses RTMP chunks
bool AnalyserRTMP::parsePacket(){
  // Get data in 1024 byte blocks
  size_t bytesNeeded = 1024;
  std::string tmpBuf;

  // Keep trying until the buffer is parseable
  while (!next.Parse(strbuf)) {
    tmpBuf = removeFromBufferBlocking(bytesNeeded);
    // If EOF is reached, whatever is left in the buffer might be parseable
    if (!isOpen()){
      if (!tmpBuf.size()){
        FAIL_MSG("Connection lost or EOF reached. Quitting...");
        return false;
      }
    }
    mediaDown += bytesNeeded;
    strbuf.append(tmpBuf);
    tmpBuf.clear();
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
    DETAIL_MED("CTRL: Abort message: %i", ntohl(*(int *)next.data.c_str()));
    // 4 bytes of stream id to drop
    break;
  case 3: // ack
    RTMPStream::snd_window_at = Bit::btohl(next.data.data());
    DETAIL_MED("CTRL: Acknowledgement: %zu", RTMPStream::snd_window_at);
    break;
  case 4:{
    short int ucmtype = ntohs(*(short int *)next.data.c_str());
    switch (ucmtype){
    case 0:
      DETAIL_MED("CTRL: User control message: stream begin %u",
                 ntohl(*(unsigned int *)(next.data.c_str() + 2)));
      break;
    case 1:
      DETAIL_MED("CTRL: User control message: stream EOF %u",
                 ntohl(*(unsigned int *)(next.data.c_str() + 2)));
      break;
    case 2:
      DETAIL_MED("CTRL: User control message: stream dry %u",
                 ntohl(*(unsigned int *)(next.data.c_str() + 2)));
      break;
    case 3:
      DETAIL_MED("CTRL: User control message: setbufferlen %u",
                 ntohl(*(unsigned int *)(next.data.c_str() + 2)));
      break;
    case 4:
      DETAIL_MED("CTRL: User control message: streamisrecorded %u",
                 ntohl(*(unsigned int *)(next.data.c_str() + 2)));
      break;
    case 6:
      DETAIL_MED("CTRL: User control message: pingrequest %u",
                 ntohl(*(unsigned int *)(next.data.c_str() + 2)));
      break;
    case 7:
      DETAIL_MED("CTRL: User control message: pingresponse %u",
                 ntohl(*(unsigned int *)(next.data.c_str() + 2)));
      break;
    case 31:
    case 32:
      // don't know, but not interesting anyway
      break;
    default:
      DETAIL_LOW("CTRL: User control message: UNKNOWN %hu - %u", ucmtype,
                 ntohl(*(unsigned int *)(next.data.c_str() + 2)));
      break;
    }
  }break;
  case 5: // window size of other end
    RTMPStream::rec_window_size = ntohl(*(int *)next.data.c_str());
    RTMPStream::rec_window_at = RTMPStream::rec_cnt;
    DETAIL_MED("CTRL: Window size: %zu", RTMPStream::rec_window_size);
    break;
  case 6:
    RTMPStream::snd_window_size = ntohl(*(int *)next.data.c_str());
    // 4 bytes window size, 1 byte limit type (ignored)
    DETAIL_MED("CTRL: Set peer bandwidth: %zu", RTMPStream::snd_window_size);
    break;
  case 8:
  case 9:
    if (detail >= 4 || reconstruct.good() || validate){
      F.ChunkLoader(next);
      mediaTime = F.tagTime();
      DETAIL_VHI("[%lu+%lu] %s", F.tagTime(), F.offset(), F.tagType().c_str());
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
