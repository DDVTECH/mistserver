#include "output_rtmp.h"
#include <cstdlib>
#include <cstring>
#include <mist/auth.h>
#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/encode.h>
#include <mist/http_parser.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <mist/util.h>
#include <sys/stat.h>

const char * trackType(char ID){
  if (ID == 8){return "audio";}
  if (ID == 9){return "video";}
  if (ID == 18){return "metadata";}
  return "unknown";
}


namespace Mist{
  OutRTMP::OutRTMP(Socket::Connection &conn) : Output(conn){
    lastSilence = 0;
    hasSilence = false;
    lastAudioInserted = 0;
    hasCustomAudio = false;
    customAudioSize = 0;
    customAudioIterator = 0;
    currentFrameTimestamp = 0;
    lastAck = Util::bootSecs();
    lastOutTime = 0;
    setRtmpOffset = false;
    rtmpOffset = 0;
    authAttempts = 0;
    maxbps = config->getInteger("maxkbps") * 128;
    //Switch realtime tracking system to mode where it never skips ahead, but only changes playback speed
    maxSkipAhead = 0;
    if (config->getString("target").size() && config->getString("target") != "-"){
      streamName = config->getString("streamname");
      pushUrl = HTTP::URL(config->getString("target"));
#ifdef SSL
      if (pushUrl.protocol != "rtmp" && pushUrl.protocol != "rtmps"){
        FAIL_MSG("Protocol not supported: %s", pushUrl.protocol.c_str());
        return;
      }
#else
      if (pushUrl.protocol != "rtmp"){
        FAIL_MSG("Protocol not supported: %s", pushUrl.protocol.c_str());
        return;
      }
#endif
      std::string app = Encodings::URL::encode(pushUrl.path, "/:=@[]");
      if (pushUrl.args.size()){app += "?" + pushUrl.args;}
      streamOut = streamName;

      size_t slash = app.find('/');
      if (slash != std::string::npos){
        streamOut = app.substr(slash + 1, std::string::npos);
        app = app.substr(0, slash);
        if (!streamOut.size()){streamOut = streamName;}
      }
      INFO_MSG("About to push stream %s out. Host: %s, port: %d, app: %s, stream: %s", streamName.c_str(),
               pushUrl.host.c_str(), pushUrl.getPort(), app.c_str(), streamOut.c_str());
      myConn.setHost(pushUrl.host);
      initialize();
      initialSeek();
      startPushOut("");
    }else{
      setBlocking(true);
      while (!conn.Received().available(1537) && conn.connected() && config->is_active){
        conn.spool();
      }
      if (!conn || !config->is_active){return;}
      RTMPStream::handshake_in.append(conn.Received().remove(1537));
      RTMPStream::rec_cnt += 1537;

      if (RTMPStream::doHandshake()){
        conn.SendNow(RTMPStream::handshake_out);
        while (!conn.Received().available(1536) && conn.connected() && config->is_active){
          conn.spool();
        }
        conn.Received().remove(1536);
        RTMPStream::rec_cnt += 1536;
        HIGH_MSG("Handshake success");
      }else{
        MEDIUM_MSG("Handshake fail (this is not a problem, usually)");
      }
      setBlocking(false);
    }
  }

  void OutRTMP::startPushOut(const char *args){

    myConn.close();
    myConn.Received().clear();

    RTMPStream::chunk_rec_max = 128;
    RTMPStream::chunk_snd_max = 128;
    RTMPStream::rec_window_size = 2500000;
    RTMPStream::snd_window_size = 2500000;
    RTMPStream::rec_window_at = 0;
    RTMPStream::snd_window_at = 0;
    RTMPStream::rec_cnt = 0;
    RTMPStream::snd_cnt = 0;

    RTMPStream::lastsend.clear();
    RTMPStream::lastrecv.clear();

    std::string app = Encodings::URL::encode(pushUrl.path, "/:=@[]");
    size_t slash = app.find('/');
    if (slash != std::string::npos){app = app.substr(0, slash);}

    if (pushUrl.protocol == "rtmp"){myConn.open(pushUrl.host, pushUrl.getPort(), false);}
#ifdef SSL
    if (pushUrl.protocol == "rtmps"){myConn.open(pushUrl.host, pushUrl.getPort(), false, true);}
#endif
    if (!myConn){
      FAIL_MSG("Could not connect to %s:%d!", pushUrl.host.c_str(), pushUrl.getPort());
      return;
    }
    // do handshake
    myConn.SendNow("\003", 1); // protocol version. Always 3
    char *temp = (char *)malloc(3072);
    if (!temp){
      myConn.close();
      return;
    }
    *((uint32_t *)temp) = 0;                         // time zero
    *(((uint32_t *)(temp + 4))) = htonl(0x01020304); // version 1 2 3 4
    for (int i = 8; i < 3072; ++i){
      temp[i] = FILLER_DATA[i % sizeof(FILLER_DATA)];
    }//"random" data
    myConn.SendNow(temp, 3072);
    free(temp);
    setBlocking(true);
    while (!myConn.Received().available(3073) && myConn.connected() && config->is_active){
      myConn.spool();
    }
    if (!myConn || !config->is_active){return;}
    myConn.Received().remove(3073);
    RTMPStream::rec_cnt += 3073;
    RTMPStream::snd_cnt += 3073;
    setBlocking(false);
    VERYHIGH_MSG("Push out handshake completed");
    std::string pushHost = "rtmp://" + pushUrl.host + "/";
    if (pushUrl.getPort() != 1935){
      pushHost = "rtmp://" + pushUrl.host + ":" + JSON::Value(pushUrl.getPort()).asString() + "/";
    }

    AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
    amfReply.addContent(AMF::Object("", "connect")); // command
    amfReply.addContent(AMF::Object("", (double)1)); // transaction ID
    amfReply.addContent(AMF::Object(""));            // options
    amfReply.getContentP(2)->addContent(AMF::Object("app", app + args));
    amfReply.getContentP(2)->addContent(AMF::Object("type", "nonprivate"));
    amfReply.getContentP(2)->addContent(
        AMF::Object("flashVer", "FMLE/3.0 (compatible; " APPNAME ")"));
    amfReply.getContentP(2)->addContent(AMF::Object("tcUrl", pushHost + app + args));
    sendCommand(amfReply, 20, 0);

    RTMPStream::chunk_snd_max = 65536;                                 // 64KiB
    myConn.SendNow(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); // send chunk size max (msg 1)
    HIGH_MSG("Waiting for server to acknowledge connect request...");
  }

  bool OutRTMP::listenMode(){return !(config->getString("target").size());}

  bool OutRTMP::onFinish(){
    MEDIUM_MSG("Finishing stream %s, %s", streamName.c_str(), myConn ? "while connected" : "already disconnected");
    if (myConn){
      if (isRecording()){
        AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
        amfreply.addContent(AMF::Object("", "deleteStream"));            // status reply
        amfreply.addContent(AMF::Object("", (double)6));                 // transaction ID
        amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); // null - command info
        amfreply.addContent(AMF::Object("", (double)1)); // No clue. But OBS sends this, too.
        sendCommand(amfreply, 20, 1);
        myConn.close();
        return false;
      }
      myConn.SendNow(RTMPStream::SendUSR(1, 1)); // send UCM StreamEOF (1), stream 1
      AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
      amfreply.addContent(AMF::Object("", "onStatus"));          // status reply
      amfreply.addContent(AMF::Object("", 0.0));                 // transaction ID
      amfreply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfreply.addContent(AMF::Object(""));                      // info
      amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Stop"));
      amfreply.getContentP(3)->addContent(AMF::Object("description", "Stream stopped"));
      amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
      amfreply.getContentP(3)->addContent(AMF::Object("clientid", 1337.0));
      sendCommand(amfreply, 20, 1);

      amfreply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
      amfreply.addContent(AMF::Object("", "onStatus"));          // status reply
      amfreply.addContent(AMF::Object("", 0.0));                 // transaction ID
      amfreply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfreply.addContent(AMF::Object(""));                      // info
      amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.UnpublishNotify"));
      amfreply.getContentP(3)->addContent(AMF::Object("description", "Stream stopped"));
      amfreply.getContentP(3)->addContent(AMF::Object("clientid", 1337.0));
      sendCommand(amfreply, 20, 1);

      myConn.close();
    }
    return false;
  }

  void OutRTMP::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "RTMP";
    capa["friendly"] = "RTMP";
    capa["desc"] = "Real time streaming over Adobe RTMP";
    capa["deps"] = "";
    capa["url_rel"] = "/play/$";
    capa["incoming_push_url"] = "rtmp://$host:$port/$password/$stream";
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
    capa["methods"][0u]["handler"] = "rtmp";
    capa["methods"][0u]["type"] = "flash/10";
    capa["methods"][0u]["hrn"] = "RTMP";
    capa["methods"][0u]["priority"] = 7;
    capa["methods"][0u]["player_url"] = "/flashplayer.swf";
    capa["optional"]["acceptable"]["name"] = "Acceptable connection types";
    capa["optional"]["acceptable"]["help"] =
        "Whether to allow only incoming pushes (2), only outgoing pulls (1), or both (0, default)";
    capa["optional"]["acceptable"]["option"] = "--acceptable";
    capa["optional"]["acceptable"]["short"] = "T";
    capa["optional"]["acceptable"]["default"] = 0;
    capa["optional"]["acceptable"]["type"] = "select";
    capa["optional"]["acceptable"]["select"][0u][0u] = 0;
    capa["optional"]["acceptable"]["select"][0u][1u] =
        "Allow both incoming and outgoing connections";
    capa["optional"]["acceptable"]["select"][1u][0u] = 1;
    capa["optional"]["acceptable"]["select"][1u][1u] = "Allow only outgoing connections";
    capa["optional"]["acceptable"]["select"][2u][0u] = 2;
    capa["optional"]["acceptable"]["select"][2u][1u] = "Allow only incoming connections";
    capa["optional"]["maxkbps"]["name"] = "Max. kbps";
    capa["optional"]["maxkbps"]["help"] =
        "Maximum bitrate to allow in the ingest direction, in kilobits per second.";
    capa["optional"]["maxkbps"]["option"] = "--maxkbps";
    capa["optional"]["maxkbps"]["short"] = "K";
    capa["optional"]["maxkbps"]["default"] = 0;
    capa["optional"]["maxkbps"]["type"] = "uint";
    cfg->addConnectorOptions(1935, capa);
    config = cfg;
    config->addStandardPushCapabilities(capa);
    capa["push_urls"].append("rtmp://*");
    capa["push_urls"].append("rtmps://*");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target RTMP URL to push out towards.";
    cfg->addOption("target", opt);
    cfg->addOption("streamname", JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":"
                                                  "\"stream\",\"help\":\"The name of the stream to "
                                                  "push out, when pushing out.\"}"));
  }

  void OutRTMP::sendSilence(uint64_t timestamp){
    /* 
    Byte 1:
      SoundFormat     = 4 bits    : AAC   = 10  = 1010
      SoundRate       = 2 bits    : 44Khz = 3   = 11
      SoundSize       = 1 bit     : always 1
      SoundType       = 1 bit     : always 1 (Stereo) for AAC
    
    Byte 2->:
      SoundData
      Since it is an AAC stream SoundData is an AACAUDIODATA object:
        AACPacketType = 8 bits     : always 1 (0 indicates sequence header which is sent in sendHeader)
        Data[N times] = 8 bits     : Raw AAC frame data
        
    tmpData: 10101111 00000001 = af 01 = \257 \001 + raw AAC silence
    */
    const char * tmpData = "\257\001!\020\004`\214\034";
    
    size_t data_len = 8;

    char rtmpheader[] ={0,                // byte 0 = cs_id | ch_type
                         0,    0, 0,      // bytes 1-3 = timestamp
                         0,    0, 0,      // bytes 4-6 = length
                         0x08,            // byte 7 = msg_type_id
                         1,    0, 0, 0,   // bytes 8-11 = msg_stream_id = 1
                         0,    0, 0, 0};  // bytes 12-15 = extended timestamp
                         
    bool allow_short = RTMPStream::lastsend.count(4);
    RTMPStream::Chunk &prev = RTMPStream::lastsend[4];
    uint8_t chtype = 0x00;
    size_t header_len = 12;
    bool time_is_diff = false;
    if (allow_short && (prev.cs_id == 4)){
      if (prev.msg_stream_id == 1){
        chtype = 0x40;
        header_len = 8; // do not send msg_stream_id
        if (data_len == prev.len && rtmpheader[7] == prev.msg_type_id){
          chtype = 0x80;
          header_len = 4; // do not send len and msg_type_id
          if (timestamp == prev.timestamp){
            chtype = 0xC0;
            header_len = 1; // do not send timestamp
          }
        }
        // override - we always sent type 0x00 if the timestamp has decreased since last chunk in this channel
        if (timestamp < prev.timestamp){
          chtype = 0x00;
          header_len = 12;
        }else{
          // store the timestamp diff instead of the whole timestamp
          timestamp -= prev.timestamp;
          time_is_diff = true;
        }
      }
    }

    // update previous chunk variables
    prev.cs_id = 4;
    prev.msg_stream_id = 1;
    prev.len = data_len;
    prev.msg_type_id = 0x08;
    if (time_is_diff){
      prev.timestamp += timestamp;
    }else{
      prev.timestamp = timestamp;
    }

    // cs_id and ch_type
    rtmpheader[0] = chtype | 4;
    // data length, 3 bytes
    rtmpheader[4] = (data_len >> 16) & 0xff;
    rtmpheader[5] = (data_len >> 8) & 0xff;
    rtmpheader[6] = data_len & 0xff;
    // timestamp, 3 bytes
    if (timestamp >= 0x00ffffff){
      // send extended timestamp
      rtmpheader[1] = 0xff;
      rtmpheader[2] = 0xff;
      rtmpheader[3] = 0xff;
      rtmpheader[header_len++] = (timestamp >> 24) & 0xff;
      rtmpheader[header_len++] = (timestamp >> 16) & 0xff;
      rtmpheader[header_len++] = (timestamp >> 8) & 0xff;
      rtmpheader[header_len++] = timestamp & 0xff;
    }else{
      // regular timestamp
      rtmpheader[1] = (timestamp >> 16) & 0xff;
      rtmpheader[2] = (timestamp >> 8) & 0xff;
      rtmpheader[3] = timestamp & 0xff;
    }

    // send the packet
    myConn.setBlocking(true);
    myConn.SendNow(rtmpheader, header_len);
    myConn.SendNow(tmpData, data_len);
    RTMPStream::snd_cnt += header_len+data_len; // update the sent data counter
    myConn.setBlocking(false);
  }
  
  // Gets next ADTS frame and loops back to 0 is EOF is reached
  void OutRTMP::calcNextFrameInfo(){ 
    // Set iterator to start of next frame
    customAudioIterator += currentFrameInfo.getCompleteSize();
    // Loop the audio
    if (customAudioIterator >= customAudioSize)
      customAudioIterator = 0;

    // Confirm syncword (= FFF)
    if (customAudioFile[customAudioIterator] != 0xFF || ( customAudioFile[customAudioIterator + 1] & 0xF0) != 0xF0 ){
      WARN_MSG("Invalid sync word at start of header. Will probably read garbage...");
    }

    uint64_t frameSize = (((customAudioFile[customAudioIterator + 3] & 0x03) << 11) 
                        | ( customAudioFile[customAudioIterator + 4] << 3)
                        |(( customAudioFile[customAudioIterator + 5] >> 5) & 0x07));
    aac::adts adtsPack(customAudioFile + customAudioIterator, frameSize);
    if (!adtsPack){
      WARN_MSG("Could not parse ADTS package. Will probably read garbage..."); 
    }
    // Update internal variables
    currentFrameInfo = adtsPack;
    currentFrameTimestamp += (adtsPack.getSampleCount() * 1000) / adtsPack.getFrequency();
  }
  
  // Sends FLV audio tag + raw AAC data
  void OutRTMP::sendLoopedAudio(uint64_t untilTimestamp){
    // ADTS frame can be invalid if there is metadata or w/e in the input file
    if ( !currentFrameInfo ){
      if (customAudioIterator == 0){
        ERROR_MSG("Input .AAC file is invalid!");
        return;
      }
      // Re-init currentFrameInfo
      WARN_MSG("File contains invalid ADTS frame. Resetting filePos to 0 and throwing this data...");
      customAudioSize = customAudioIterator;
      customAudioIterator = 0;
      // NOTE that we do not reset the timestamp to prevent eternal loops
      
      // Confirm syncword (= FFF)
      if (customAudioFile[customAudioIterator] != 0xFF || ( customAudioFile[customAudioIterator + 1] & 0xF0) != 0xF0 ){
        WARN_MSG("Invalid sync word at start of header. Invalid input file!");
        return;
      }

      uint64_t frameSize = (((customAudioFile[customAudioIterator + 3] & 0x03) << 11) | ( customAudioFile[customAudioIterator + 4] << 3) | (( customAudioFile[customAudioIterator + 5] >> 5) & 0x07));
      aac::adts adtsPack(customAudioFile + customAudioIterator, frameSize);
      if (!adtsPack){
        WARN_MSG("Could not parse ADTS package. Invalid input file!");
        return;
      }
      currentFrameInfo = adtsPack;
    }
    
    // Keep parsing ADTS frames until we reach a frame which starts in the future
    while (currentFrameTimestamp < untilTimestamp){  
      // Init RTMP header info
      char rtmpheader[] ={0,              // byte 0 = cs_id | ch_type
                          0,    0, 0,     // bytes 1-3 = timestamp
                          0,    0, 0,     // bytes 4-6 = length
                          0x08,           // byte 7 = msg_type_id
                          1,    0, 0, 0,  // bytes 8-11 = msg_stream_id = 1
                          0,    0, 0, 0}; // bytes 12-15 = extended timestamp
                          
      // Separate timestamp since we store Δtimestamps
      uint64_t rtmpTimestamp = currentFrameTimestamp;
      // Since we have to prepend an FLV audio tag, increase size by 2 bytes
      uint64_t aacPacketSize = currentFrameInfo.getPayloadSize() + 2;
      // If there is a previous sent package, we do not need to send all data
      bool allow_short = RTMPStream::lastsend.count(4);
      RTMPStream::Chunk &prev = RTMPStream::lastsend[4];
      // Defines the type of header. Only the 2 most significant bits are counted:
      //  0x00 = 000.. = 12 byte header
      //  0x40 = 010.. = 8 byte header, leave out message ID if it's the same as prev
      //  0x80 = 100.. = 4 byte header, above + leave out msg type and size if the packets are all the same size and type
      //  0xC0 = 110.. = 1 byte header, above + leave out timestamp as well
      uint8_t chtype = 0x00;
      size_t header_len = 12;
      bool time_is_diff = false;
      if (allow_short && (prev.cs_id == 4)){
        if (prev.msg_stream_id == 1){
          chtype = 0x40;
          header_len = 8;
          if (aacPacketSize == prev.len && rtmpheader[7] == prev.msg_type_id){
            chtype = 0x80;
            header_len = 4;
            if (rtmpTimestamp == prev.timestamp){
              chtype = 0xC0;
              header_len = 1;
            }
          }
          // override - we always sent type 0x00 if the timestamp has decreased since last chunk in this channel
          if (rtmpTimestamp < prev.timestamp){
            chtype = 0x00;
            header_len = 12;
          }else{
            // store the timestamp diff instead of the whole timestamp
            rtmpTimestamp -= prev.timestamp;
            time_is_diff = true;
          }
        }
      }

      // Update previous chunk variables
      prev.cs_id = 4;
      prev.msg_stream_id = 1;
      prev.len = aacPacketSize;
      prev.msg_type_id = 0x08;
      if (time_is_diff){
        prev.timestamp += rtmpTimestamp;
      }else{
        prev.timestamp = rtmpTimestamp;
      }

      // Now fill in type...
      rtmpheader[0] = chtype | 4;
      // data length...
      rtmpheader[4] = (aacPacketSize >> 16) & 0xff;
      rtmpheader[5] = (aacPacketSize >> 8) & 0xff;
      rtmpheader[6] = aacPacketSize & 0xff;
      // and timestamp (3 bytes unless extended)
      if (rtmpTimestamp >= 0x00ffffff){
        rtmpheader[1] = 0xff;
        rtmpheader[2] = 0xff;
        rtmpheader[3] = 0xff;
        rtmpheader[header_len++] = (rtmpTimestamp >> 24) & 0xff;
        rtmpheader[header_len++] = (rtmpTimestamp >> 16) & 0xff;
        rtmpheader[header_len++] = (rtmpTimestamp >> 8) & 0xff;
        rtmpheader[header_len++] = rtmpTimestamp & 0xff;
      }else{
        rtmpheader[1] = (rtmpTimestamp >> 16) & 0xff;
        rtmpheader[2] = (rtmpTimestamp >> 8) & 0xff;
        rtmpheader[3] = rtmpTimestamp & 0xff;
      }

      // Send RTMP packet containing header only
      myConn.setBlocking(true);
      myConn.SendNow(rtmpheader, header_len);
      // Prepend FLV AAC audio tag
      char *tmpData = (char*)malloc(aacPacketSize);
      const char *tmpBuf = currentFrameInfo.getPayload();
      // Prepend FLV Audio tag: always 10101111 00000001 + raw AAC
      tmpData[0] = '\257';
      tmpData[1] = '\001';
      for (int i = 2; i < aacPacketSize; i++)
        tmpData[i] = tmpBuf[i-2];

      myConn.SendNow(tmpData, aacPacketSize);
      // Update internal variables
      RTMPStream::snd_cnt += header_len+aacPacketSize;
      myConn.setBlocking(false);
      
      // get next ADTS frame for new raw AAC data
      calcNextFrameInfo();
    }
  }
  
  
  void OutRTMP::sendNext(){
    //Every 5s, check if the track selection should change in live streams, and do it.
    if (M.getLive()){
      static uint64_t lastMeta = 0;
      if (Util::epoch() > lastMeta + 5){
        lastMeta = Util::epoch();
        if (selectDefaultTracks()){
          INFO_MSG("Track selection changed - resending headers and continuing");
          sentHeader = false;
          return;
        }
      }
      if (liveSeek()){return;}
    }

    if (streamOut.size()){
      if (thisPacket.getTime() - rtmpOffset < lastOutTime){
        int64_t OLD = rtmpOffset;
        rtmpOffset -= (1 + lastOutTime - (thisPacket.getTime() - rtmpOffset));
        INFO_MSG("Changing rtmpOffset from %" PRId64 " to %" PRId64, OLD, rtmpOffset);
        realTime = 800;
      }
      lastOutTime = thisPacket.getTime() - rtmpOffset;
    }
    uint64_t timestamp = thisPacket.getTime() - rtmpOffset;
    // make sure we don't go negative
    if (rtmpOffset > (int64_t)thisPacket.getTime()){
      timestamp = 0;
      rtmpOffset = (int64_t)thisPacket.getTime();
    }

    // Send silence packets if needed
    if (hasSilence){
      // If there's more than 15s of skip, skip audio as well
      if (timestamp > 15000 && lastAudioInserted < timestamp - 15000){
        lastAudioInserted = timestamp - 30;
      }
      // convert time to packet counter
      uint64_t currSilence = ((lastAudioInserted*44100+512000)/1024000)+1;
      uint64_t silentTime = currSilence*1024000/44100;
      // keep sending silent packets until we've caught up to the current timestamp
      while (silentTime < timestamp){
        sendSilence(silentTime);
        lastAudioInserted = silentTime;
        silentTime = (++currSilence)*1024000/44100;
      }
    }

    // NOTE hier ergens fixen dat de audio ook stopt als video wegvalt
    
    // Send looped audio if needed
    if (hasCustomAudio){
      // If there's more than 15s of skip, skip audio as well
      if (timestamp > 15000 && lastAudioInserted < timestamp - 15000){
        lastAudioInserted = timestamp - 30;
      }
      // keep sending silent packets until we've caught up to the current timestamp
      sendLoopedAudio(timestamp);
      lastAudioInserted = timestamp;
    }

    char rtmpheader[] ={ 0,              // byte 0 = cs_id | ch_type
                         0,    0, 0,     // bytes 1-3 = timestamp
                         0,    0, 0,     // bytes 4-6 = length
                         0x12,           // byte 7 = msg_type_id
                         1,    0, 0, 0,  // bytes 8-11 = msg_stream_id = 1
                         0,    0, 0, 0}; // bytes 12-15 = extended timestamp
    char dataheader[] ={0, 0, 0, 0, 0};
    unsigned int dheader_len = 1;
    static Util::ResizeablePointer swappy;
    char *tmpData = 0;   // pointer to raw media data
    size_t data_len = 0; // length of processed media data
    thisPacket.getString("data", tmpData, data_len);
    
    std::string type = M.getType(thisIdx);
    std::string codec = M.getCodec(thisIdx);

    // set msg_type_id
    if (type == "video"){
      rtmpheader[7] = 0x09;
      if (codec == "H264"){
        dheader_len += 4;
        dataheader[0] = 7;
        dataheader[1] = 1;
        int64_t offset = thisPacket.getInt("offset");
        if (offset){
          dataheader[2] = (offset >> 16) & 0xFF;
          dataheader[3] = (offset >> 8) & 0xFF;
          dataheader[4] = offset & 0xFF;
        }
      }
      if (codec == "H263"){dataheader[0] = 2;}
      dataheader[0] |= (thisPacket.getFlag("keyframe") ? 0x10 : 0x20);
      if (thisPacket.getFlag("disposableframe")){dataheader[0] |= 0x30;}
    }

    if (type == "audio"){
      uint32_t rate = M.getRate(thisIdx);
      rtmpheader[7] = 0x08;
      if (codec == "AAC"){
        dataheader[0] += 0xA0;
        dheader_len += 1;
        dataheader[1] = 1; // raw AAC data, not sequence header
      }
      if (codec == "MP3"){
        dataheader[0] += 0x20;
        dataheader[0] |= (rate == 8000 ? 0xE0 : 0x20);
      }
      if (codec == "ADPCM"){dataheader[0] |= 0x10;}
      if (codec == "PCM"){
        if (M.getSize(thisIdx) == 16 && swappy.allocate(data_len)){
          for (uint32_t i = 0; i < data_len; i += 2){
            swappy[i] = tmpData[i + 1];
            swappy[i + 1] = tmpData[i];
          }
          tmpData = swappy;
        }
        dataheader[0] |= 0x30;
      }
      if (codec == "Nellymoser"){
        dataheader[0] |= (rate == 8000 ? 0x50 : (rate == 16000 ? 0x40 : 0x60));
      }
      if (codec == "ALAW"){dataheader[0] |= 0x70;}
      if (codec == "ULAW"){dataheader[0] |= 0x80;}
      if (codec == "Speex"){dataheader[0] |= 0xB0;}

      if (rate >= 44100){
        dataheader[0] |= 0x0C;
      }else if (rate >= 22050){
        dataheader[0] |= 0x08;
      }else if (rate >= 11025){
        dataheader[0] |= 0x04;
      }
      if (M.getSize(thisIdx) != 8){dataheader[0] |= 0x02;}
      if (M.getChannels(thisIdx) > 1){dataheader[0] |= 0x01;}
    }
    data_len += dheader_len;

    bool allow_short = RTMPStream::lastsend.count(4);
    RTMPStream::Chunk &prev = RTMPStream::lastsend[4];
    uint8_t chtype = 0x00;
    size_t header_len = 12;
    bool time_is_diff = false;
    if (allow_short && (prev.cs_id == 4)){
      if (prev.msg_stream_id == 1){
        chtype = 0x40;
        header_len = 8; // do not send msg_stream_id
        if (data_len == prev.len && rtmpheader[7] == prev.msg_type_id){
          chtype = 0x80;
          header_len = 4; // do not send len and msg_type_id
          if (timestamp == prev.timestamp){
            chtype = 0xC0;
            header_len = 1; // do not send timestamp
          }
        }
        // override - we always sent type 0x00 if the timestamp has decreased since last chunk in this channel
        if (timestamp < prev.timestamp){
          chtype = 0x00;
          header_len = 12;
        }else{
          // store the timestamp diff instead of the whole timestamp
          timestamp -= prev.timestamp;
          time_is_diff = true;
        }
      }
    }

    // update previous chunk variables
    prev.cs_id = 4;
    prev.msg_stream_id = 1;
    prev.len = data_len;
    prev.msg_type_id = rtmpheader[7];
    if (time_is_diff){
      prev.timestamp += timestamp;
    }else{
      prev.timestamp = timestamp;
    }

    // cs_id and ch_type
    rtmpheader[0] = chtype | 4;
    // data length, 3 bytes
    rtmpheader[4] = (data_len >> 16) & 0xff;
    rtmpheader[5] = (data_len >> 8) & 0xff;
    rtmpheader[6] = data_len & 0xff;
    // timestamp, 3 bytes
    if (timestamp >= 0x00ffffff){
      // send extended timestamp
      rtmpheader[1] = 0xff;
      rtmpheader[2] = 0xff;
      rtmpheader[3] = 0xff;
      rtmpheader[header_len++] = (timestamp >> 24) & 0xff;
      rtmpheader[header_len++] = (timestamp >> 16) & 0xff;
      rtmpheader[header_len++] = (timestamp >> 8) & 0xff;
      rtmpheader[header_len++] = timestamp & 0xff;
    }else{
      // regular timestamp
      rtmpheader[1] = (timestamp >> 16) & 0xff;
      rtmpheader[2] = (timestamp >> 8) & 0xff;
      rtmpheader[3] = timestamp & 0xff;
    }

    // send the header
    myConn.setBlocking(true);
    myConn.SendNow(rtmpheader, header_len);
    RTMPStream::snd_cnt += header_len; // update the sent data counter
    // set the header's first byte to the "continue" type chunk, for later use
    rtmpheader[0] = 0xC4;
    if (timestamp >= 0x00ffffff){
      rtmpheader[1] = (timestamp >> 24) & 0xff;
      rtmpheader[2] = (timestamp >> 16) & 0xff;
      rtmpheader[3] = (timestamp >> 8) & 0xff;
      rtmpheader[4] = timestamp & 0xff;
    }

    // sent actual data - never send more than chunk_snd_max at a time
    // interleave blocks of max chunk_snd_max bytes with 0xC4 bytes to indicate continue
    size_t len_sent = 0;
    while (len_sent < data_len){
      size_t to_send = std::min(data_len - len_sent, RTMPStream::chunk_snd_max);
      if (!len_sent){
        myConn.SendNow(dataheader, dheader_len);
        RTMPStream::snd_cnt += dheader_len; // update the sent data counter
        to_send -= dheader_len;
        len_sent += dheader_len;
      }
      myConn.SendNow(tmpData + len_sent - dheader_len, to_send);
      len_sent += to_send;
      if (len_sent < data_len){
        if (timestamp >= 0x00ffffff){
          myConn.SendNow(rtmpheader, 5);
          RTMPStream::snd_cnt += 5; // update the sent data counter
        }else{
          myConn.SendNow(rtmpheader, 1);
          RTMPStream::snd_cnt += 1; // update the sent data counter
        }
      }
    }
    myConn.setBlocking(false);
  }

  void OutRTMP::sendHeader(){
    FLV::Tag tag;
    std::set<size_t> selectedTracks;
    // Will contain the full audio=<> parameter in it, which should be CSV's of
    // {path, url, filename, silent, silence}1..*
    std::string audioParameterBuffer;
    // Current parameter we're parsing
    std::string audioParameter;
    // Indicates position where the previous parameter ended
    int prevPos = 0;
    // Used to read a custom AAC file 
    HTTP::URIReader inAAC;
    char *tempBuffer;
    size_t bytesRead;
    
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      selectedTracks.insert(it->first);
    }
    tag.DTSCMetaInit(meta, selectedTracks);
    if (tag.len){
      tag.tagTime(currentTime() - rtmpOffset);
      myConn.SendNow(RTMPStream::SendMedia(tag));
    }

    for (std::set<size_t>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      std::string type = M.getType(*it);
      if (type == "video"){
        if (tag.DTSCVideoInit(meta, *it)){
          tag.tagTime(currentTime() - rtmpOffset);
          myConn.SendNow(RTMPStream::SendMedia(tag));
        }
      }
      if (type == "audio"){
        if (tag.DTSCAudioInit(meta.getCodec(*it), meta.getRate(*it), meta.getSize(*it), meta.getChannels(*it), meta.getInit(*it))){
          tag.tagTime(currentTime() - rtmpOffset);
          myConn.SendNow(RTMPStream::SendMedia(tag));
        }
      }
    }
    // Insert silent init data if audio set to silent or loop a custom AAC file
    audioParameterBuffer = targetParams["audio"];
    HIGH_MSG("audioParameterBuffer: %s", audioParameterBuffer.c_str());
    // Read until we find a , or end of audioParameterBuffer
    for (std::string::size_type i = 0; i < audioParameterBuffer.size(); i++){
      if ( (audioParameterBuffer[i] == ',') || i + 1 == (audioParameterBuffer.size()) ){
        // If end of buffer reached, take entire string
        if (i + 1 == audioParameterBuffer.size()){i++;}
        // Get audio parameter
        audioParameter = audioParameterBuffer.substr(prevPos, i - prevPos);
        HIGH_MSG("Parsing audio parameter %s", audioParameter.c_str());
        // Inc i to skip the ,
        i++; 
        prevPos = i;
        
        if (audioParameter == "silence" || audioParameter == "silent"){
          hasSilence = true;
          INFO_MSG("Filling audio track with silence");
          break;
        }
        // Else parse AAC track(s) until we find one which works for us
        else{
          inAAC.open(audioParameter);
          if (inAAC && !inAAC.isEOF()){
              inAAC.readAll(tempBuffer, bytesRead);
              customAudioSize = bytesRead;
              // Copy to buffer since inAAC will be closed soon...
              customAudioFile = (char*)malloc(bytesRead);
              memcpy(customAudioFile, tempBuffer, bytesRead);
              hasCustomAudio = true;
              customAudioIterator = 0;
              break;
          }
          else{
            INFO_MSG("Could not parse audio parameter %s. Skipping...", audioParameter.c_str());
          }
        }
      }
    }
    if (hasSilence && tag.DTSCAudioInit("AAC", 44100, 32, 2, std::string("\022\020V\345\000", 5))){
      // InitData contains AudioSpecificConfig:
      //                   \022       \020       V        \345     \000
      //   12 10 56 e5 0 = 00010-010  0-0010-000 01010110 11100101 00000000
      //                   Type -sample-chnl-000
      //                   AACLC-44100 -2   -000
      INFO_MSG("Inserting silence track init data");
      tag.tagTime(currentTime() - rtmpOffset);
      myConn.SendNow(RTMPStream::SendMedia(tag));
    }
    if (hasCustomAudio){
      // Get first frame in order to init the audio track correctly
      // Confirm syncword (= FFF)
      if (customAudioFile[customAudioIterator] != 0xFF || ( customAudioFile[customAudioIterator + 1] & 0xF0) != 0xF0 ){
        WARN_MSG("Invalid sync word at start of header. Invalid input file!");
        return;
      }
      // Calculate the starting position of the next frame
      uint64_t frameSize = (((customAudioFile[customAudioIterator + 3] & 0x03) << 11) | ( customAudioFile[customAudioIterator + 4] << 3) | (( customAudioFile[customAudioIterator + 5] >> 5) & 0x07));
    
      // Create ADTS object of frame
      aac::adts adtsPack(customAudioFile + customAudioIterator, frameSize);
      if (!adtsPack){
        WARN_MSG("Could not parse ADTS package. Invalid input file!");
        return;
      }
      currentFrameInfo = adtsPack;
      char *tempInitData = (char*)malloc(2);
      /* 
       * Create AudioSpecificConfig
       * DTSCAudioInit already includes the sequence header at pos 12
       * We need:
       *  objectType       = getAACProfile (5 bits) (probably 00001 AAC Main or 00010 AACLC)
       *  Sampling Rate    = 44100    = 0100
       *  Channels         = 2        = 0010
       *  + 000
       */
      tempInitData[0] = 0x02 + (currentFrameInfo.getAACProfile() << 3);
      tempInitData[1] = 0x10;
      const std::string initData = std::string(tempInitData, 2);
      
      if (tag.DTSCAudioInit("AAC", currentFrameInfo.getFrequency(), currentFrameInfo.getSampleCount(), currentFrameInfo.getChannelCount(), initData)){
        INFO_MSG("Loaded a %" PRIu64 " byte custom audio file as audio loop", customAudioSize);
        myConn.SendNow(RTMPStream::SendMedia(tag));
      }
    }

    sentHeader = true;
  }

  void OutRTMP::requestHandler(){
    // If needed, slow down the reading to a rate of maxbps on average
    static bool slowWarned = false;
    if (maxbps && (Util::bootSecs() - myConn.connTime()) &&
        myConn.dataDown() / (Util::bootSecs() - myConn.connTime()) > maxbps){
      if (!slowWarned){
        WARN_MSG("Slowing down connection from %s because rate of %" PRIu64 "kbps > %" PRIu32
                 "kbps",
                 getConnectedHost().c_str(),
                 (myConn.dataDown() / (Util::bootSecs() - myConn.connTime())) / 128, maxbps / 128);
        slowWarned = true;
      }
      Util::sleep(50);
    }
    Output::requestHandler();
  }

  void OutRTMP::onRequest(){parseChunk(myConn.Received());}

  ///\brief Sends a RTMP command either in AMF or AMF3 mode.
  ///\param amfReply The data to be sent over RTMP.
  ///\param messageType The type of message.
  ///\param streamId The ID of the AMF stream.
  void OutRTMP::sendCommand(AMF::Object &amfReply, int messageType, int streamId){
    HIGH_MSG("Sending: %s", amfReply.Print().c_str());
    if (messageType == 17){
      myConn.SendNow(RTMPStream::SendChunk(3, messageType, streamId, (char)0 + amfReply.Pack()));
    }else{
      myConn.SendNow(RTMPStream::SendChunk(3, messageType, streamId, amfReply.Pack()));
    }
  }// sendCommand

  ///\brief Parses a single AMF command message, and sends a direct response through sendCommand().
  ///\param amfData The received request.
  ///\param messageType The type of message.
  ///\param streamId The ID of the AMF stream.
  /// \triggers
  /// The `"STREAM_PUSH"` trigger is stream-specific, and is ran right before an incoming push is
  /// accepted. If cancelled, the push is denied. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// connected client host
  /// output handler name
  /// request URL (if any)
  /// ~~~~~~~~~~~~~~~
  /// The `"RTMP_PUSH_REWRITE"` trigger is global and ran right before an RTMP publish request is
  /// parsed. It cannot be cancelled, but an invalid URL can be returned; which is effectively
  /// equivalent to cancelling. This trigger is special: the response is used as RTMP URL override,
  /// and not handled as normal. If used, the handler for this trigger MUST return a valid RTMP URL
  /// to allow the push to go through. If used multiple times, the last defined handler overrides
  /// any and all previous handlers. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// current RTMP URL
  /// connected client host
  /// ~~~~~~~~~~~~~~~
  void OutRTMP::parseAMFCommand(AMF::Object &amfData, int messageType, int streamId){
    MEDIUM_MSG("Received command: %s", amfData.Print().c_str());
    HIGH_MSG("AMF0 command: %s", amfData.getContentP(0)->StrValue().c_str());
    if (amfData.getContentP(0)->StrValue() == "xsbwtest"){
      // send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_error")); // result success
      amfReply.addContent(amfData.getContent(1));     // same transaction ID
      amfReply.addContent(AMF::Object("", amfData.getContentP(0)->StrValue())); // null - command info
      amfReply.addContent(AMF::Object("", "Hai XSplit user!"));                 // stream ID?
      sendCommand(amfReply, messageType, streamId);
      return;
    }
    if (amfData.getContentP(0)->StrValue() == "connect"){
      double objencoding = 0;
      if (amfData.getContentP(2)->getContentP("objectEncoding")){
        objencoding = amfData.getContentP(2)->getContentP("objectEncoding")->NumValue();
      }
      if (amfData.getContentP(2)->getContentP("flashVer")){
        UA = amfData.getContentP(2)->getContentP("flashVer")->StrValue();
      }
      app_name = amfData.getContentP(2)->getContentP("tcUrl")->StrValue();
      reqUrl = app_name; // LTS
      app_name = app_name.substr(app_name.find('/', 7) + 1);

      // If this user agent matches, we can safely guess it's librtmp, and this is not dangerous
      if (UA == "FMLE/3.0 (compatible; FMSc/1.0)"){
        // set max chunk size early, to work around OBS v25 bug
        RTMPStream::chunk_snd_max = 65536;                                 // 64KiB
        myConn.SendNow(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); // send chunk size max (msg 1)
      }
      // send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); // result success
      amfReply.addContent(amfData.getContent(1));      // same transaction ID
      amfReply.addContent(AMF::Object(""));            // server properties
      amfReply.getContentP(2)->addContent(AMF::Object("fmsVer", "FMS/3,5,5,2004"));
      amfReply.getContentP(2)->addContent(AMF::Object("capabilities", 31.0));
      amfReply.getContentP(2)->addContent(AMF::Object("mode", 1.0));
      amfReply.addContent(AMF::Object("")); // info
      amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfReply.getContentP(3)->addContent(AMF::Object("code", "NetConnection.Connect.Success"));
      amfReply.getContentP(3)->addContent(AMF::Object("description", "Connection succeeded."));
      amfReply.getContentP(3)->addContent(AMF::Object("clientid", 1337.0));
      amfReply.getContentP(3)->addContent(AMF::Object("objectEncoding", objencoding));
      // amfReply.getContentP(3)->addContent(AMF::Object("data", AMF::AMF0_ECMA_ARRAY));
      // amfReply.getContentP(3)->getContentP(4)->addContent(AMF::Object("version", "3,5,4,1004"));
      sendCommand(amfReply, messageType, streamId);
      // Send other stream-related packets
      RTMPStream::chunk_snd_max = 65536;                                 // 64KiB
      myConn.SendNow(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); // send chunk size max (msg 1)
      myConn.SendNow(RTMPStream::SendCTL(5, RTMPStream::snd_window_size)); // send window acknowledgement size (msg 5)
      myConn.SendNow(RTMPStream::SendCTL(6, RTMPStream::rec_window_size)); // send rec window acknowledgement size (msg 6)
      // myConn.SendNow(RTMPStream::SendUSR(0, 1)); //send UCM StreamBegin (0), stream 1
      // send onBWDone packet - no clue what it is, but real server sends it...
      // amfReply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
      // amfReply.addContent(AMF::Object("", "onBWDone"));//result
      // amfReply.addContent(amfData.getContent(1));//same transaction ID
      // amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null
      // sendCommand(amfReply, messageType, streamId);
      return;
    }// connect
    if (amfData.getContentP(0)->StrValue() == "createStream"){
      // send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result"));           // result success
      amfReply.addContent(amfData.getContent(1));                // same transaction ID
      amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfReply.addContent(AMF::Object("", 1.0));                 // stream ID - we use 1
      sendCommand(amfReply, messageType, streamId);
      // myConn.SendNow(RTMPStream::SendUSR(0, 1)); //send UCM StreamBegin (0), stream 1
      return;
    }// createStream
    if (amfData.getContentP(0)->StrValue() == "ping"){
      // send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result"));           // result success
      amfReply.addContent(amfData.getContent(1));                // same transaction ID
      amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfReply.addContent(AMF::Object("", "Pong!"));             // stream ID - we use 1
      sendCommand(amfReply, messageType, streamId);
      return;
    }// createStream
    if (amfData.getContentP(0)->StrValue() == "closeStream"){
      myConn.SendNow(RTMPStream::SendUSR(1, 1)); // send UCM StreamEOF (1), stream 1
      AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
      amfreply.addContent(AMF::Object("", "onStatus"));          // status reply
      amfreply.addContent(AMF::Object("", 0.0));                 // transaction ID
      amfreply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfreply.addContent(AMF::Object(""));                      // info
      amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Stop"));
      amfreply.getContentP(3)->addContent(AMF::Object("description", "Stream stopped"));
      amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
      amfreply.getContentP(3)->addContent(AMF::Object("clientid", 1337.0));
      sendCommand(amfreply, 20, 1);
      stop();
      return;
    }
    if (amfData.getContentP(0)->StrValue() == "deleteStream"){
      stop();
      return;
    }
    if ((amfData.getContentP(0)->StrValue() == "FCUnpublish") ||
        (amfData.getContentP(0)->StrValue() == "releaseStream")){
      // ignored
      return;
    }
    if ((amfData.getContentP(0)->StrValue() == "FCSubscribe")){
      // send a FCPublish reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "onFCSubscribe"));     // status reply
      amfReply.addContent(amfData.getContent(1));                // same transaction ID
      amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfReply.addContent(AMF::Object(""));                      // info
      amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Start"));
      amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfReply.getContentP(3)->addContent(
          AMF::Object("description",
                      "Please follow up with play or publish command, as we ignore this command."));
      sendCommand(amfReply, messageType, streamId);
      return;
    }// FCPublish
    if ((amfData.getContentP(0)->StrValue() == "FCPublish")){
      // send a FCPublish reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "onFCPublish"));       // status reply
      amfReply.addContent(amfData.getContent(1));                // same transaction ID
      amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfReply.addContent(AMF::Object(""));                      // info
      amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Publish.Start"));
      amfReply.getContentP(3)->addContent(AMF::Object(
          "description", "Please follow up with publish command, as we ignore this command."));
      sendCommand(amfReply, messageType, streamId);
      return;
    }// FCPublish
    if (amfData.getContentP(0)->StrValue() == "releaseStream"){
      // send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result"));           // result success
      amfReply.addContent(amfData.getContent(1));                // same transaction ID
      amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfReply.addContent(AMF::Object("", AMF::AMF0_UNDEFINED)); // stream ID?
      sendCommand(amfReply, messageType, streamId);
      return;
    }// releaseStream
    if ((amfData.getContentP(0)->StrValue() == "getStreamLength") ||
        (amfData.getContentP(0)->StrValue() == "getMovLen")){
      // send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result"));           // result success
      amfReply.addContent(amfData.getContent(1));                // same transaction ID
      amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfReply.addContent(AMF::Object("", 0.0));                 // zero length
      sendCommand(amfReply, messageType, streamId);
      return;
    }// getStreamLength
    if ((amfData.getContentP(0)->StrValue() == "publish")){
      if (config->getInteger("acceptable") == 1){// Only allow outgoing ( = 1)? Abort!
        AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
        amfReply.addContent(AMF::Object("", "_error"));                  // result success
        amfReply.addContent(amfData.getContent(1));                      // same transaction ID
        amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); // null - command info
        amfReply.addContent(AMF::Object(""));                            // info
        amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Publish.Rejected"));
        amfReply.getContentP(3)->addContent(AMF::Object(
            "description", "Publish rejected: this interface does not allow publishing"));
        sendCommand(amfReply, messageType, streamId);
        INFO_MSG("Push from %s rejected - connector configured to only allow outgoing streams",
                 getConnectedHost().c_str());
        onFinish();
        return;
      }
      if (amfData.getContentP(3)){
        streamName = Encodings::URL::decode(amfData.getContentP(3)->StrValue());
        reqUrl += "/" + streamName; // LTS

        // handle variables
        if (streamName.find('?') != std::string::npos){
          std::string tmpVars = streamName.substr(streamName.find('?') + 1);
          streamName = streamName.substr(0, streamName.find('?'));
          HTTP::parseVars(tmpVars, targetParams);
        }
        //Remove anything before the last slash
        if (streamName.find('/')){
          streamName = streamName.substr(0, streamName.find('/'));
        }
        Util::setStreamName(streamName);

        /*LTS-START*/
        if (Triggers::shouldTrigger("RTMP_PUSH_REWRITE")){
          std::string payload = reqUrl + "\n" + getConnectedHost();
          std::string newUrl = reqUrl;
          Triggers::doTrigger("RTMP_PUSH_REWRITE", payload, "", false, newUrl);
          if (!newUrl.size()){
            FAIL_MSG("Push from %s to URL %s rejected - RTMP_PUSH_REWRITE trigger blanked the URL",
                     getConnectedHost().c_str(), reqUrl.c_str());
            onFinish();
            return;
          }
          reqUrl = newUrl;
          size_t lSlash = newUrl.rfind('/');
          if (lSlash != std::string::npos){
            streamName = newUrl.substr(lSlash + 1);
          }else{
            streamName = newUrl;
          }
          // handle variables
          if (streamName.find('?') != std::string::npos){
            std::string tmpVars = streamName.substr(streamName.find('?') + 1);
            streamName = streamName.substr(0, streamName.find('?'));
            HTTP::parseVars(tmpVars, targetParams);
          }
          Util::setStreamName(streamName);
        }
        /*LTS-END*/

        size_t colonPos = streamName.find(':');
        if (colonPos != std::string::npos && colonPos < 6){
          std::string oldName = streamName;
          if (std::string(".") + oldName.substr(0, colonPos) == oldName.substr(oldName.size() - colonPos - 1)){
            streamName = oldName.substr(colonPos + 1);
          }else{
            streamName = oldName.substr(colonPos + 1) + std::string(".") + oldName.substr(0, colonPos);
          }
          Util::setStreamName(streamName);
        }

        Util::sanitizeName(streamName);

        if (Triggers::shouldTrigger("PUSH_REWRITE")){
          std::string payload = reqUrl + "\n" + getConnectedHost() + "\n" + streamName;
          std::string newStream = streamName;
          Triggers::doTrigger("PUSH_REWRITE", payload, "", false, newStream);
          if (!newStream.size()){
            FAIL_MSG("Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                     getConnectedHost().c_str(), reqUrl.c_str());
            Util::logExitReason(
                "Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                getConnectedHost().c_str(), reqUrl.c_str());
            onFinish();
            return;
          }else{
            streamName = newStream;
            // handle variables
            if (streamName.find('?') != std::string::npos){
              std::string tmpVars = streamName.substr(streamName.find('?') + 1);
              streamName = streamName.substr(0, streamName.find('?'));
              HTTP::parseVars(tmpVars, targetParams);
            }
            Util::sanitizeName(streamName);
            Util::setStreamName(streamName);
          }
        }

        if (!allowPush(app_name)){
          onFinish();
          return;
        }
      }
      // send a status reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "onStatus"));          // status reply
      amfReply.addContent(amfData.getContent(1));                // same transaction ID
      amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfReply.addContent(AMF::Object(""));                      // info
      amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Publish.Start"));
      amfReply.getContentP(3)->addContent(AMF::Object("description", "Stream is now published!"));
      amfReply.getContentP(3)->addContent(AMF::Object("clientid", 1337.0));
      sendCommand(amfReply, messageType, streamId);
      /*
      //send a _result reply
      amfReply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("", 1, AMF::AMF0_BOOL)); //publish success?
      sendCommand(amfReply, messageType, streamId);
      */
      myConn.SendNow(RTMPStream::SendUSR(0, 1)); // send UCM StreamBegin (0), stream 1
      return;
    }// getStreamLength
    if (amfData.getContentP(0)->StrValue() == "checkBandwidth"){
      // send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result"));           // result success
      amfReply.addContent(amfData.getContent(1));                // same transaction ID
      amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      sendCommand(amfReply, messageType, streamId);
      return;
    }// checkBandwidth
    if (amfData.getContentP(0)->StrValue() == "onBWDone"){return;}
    if ((amfData.getContentP(0)->StrValue() == "play") ||
        (amfData.getContentP(0)->StrValue() == "play2")){
      // set reply number and stream name, actual reply is sent up in the ss.spool() handler
      double playTransaction = amfData.getContentP(1)->NumValue();
      int8_t playMessageType = messageType;
      int32_t playStreamId = streamId;
      streamName = Encodings::URL::decode(amfData.getContentP(3)->StrValue());
      reqUrl += "/" + streamName; // LTS

      // handle variables
      if (streamName.find('?') != std::string::npos){
        std::string tmpVars = streamName.substr(streamName.find('?') + 1);
        streamName = streamName.substr(0, streamName.find('?'));
        HTTP::parseVars(tmpVars, targetParams);
      }

      size_t colonPos = streamName.find(':');
      if (colonPos != std::string::npos && colonPos < 6){
        std::string oldName = streamName;
        if (std::string(".") + oldName.substr(0, colonPos) == oldName.substr(oldName.size() - colonPos - 1)){
          streamName = oldName.substr(colonPos + 1);
        }else{
          streamName = oldName.substr(colonPos + 1) + std::string(".") + oldName.substr(0, colonPos);
        }
      }
      Util::sanitizeName(streamName);
      Util::setStreamName(streamName);

      if (config->getInteger("acceptable") == 2){// Only allow incoming ( = 2)? Abort!
        AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
        amfReply.addContent(AMF::Object("", "_error"));                  // result success
        amfReply.addContent(amfData.getContent(1));                      // same transaction ID
        amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); // null - command info
        amfReply.addContent(AMF::Object(""));                            // info
        amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Rejected"));
        amfReply.getContentP(3)->addContent(
            AMF::Object("description", "Play rejected: this interface does not allow playback"));
        sendCommand(amfReply, messageType, streamId);
        INFO_MSG("Play of %s by %s rejected - connector configured to only allow incoming streams",
                 streamName.c_str(), getConnectedHost().c_str());
        onFinish();
        return;
      }

      initialize();
      //Abort if stream could not be opened
      if (!M){
        INFO_MSG("Could not open stream, aborting");
        // send a _result reply
        AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
        amfReply.addContent(AMF::Object("", "_error"));                           // result success
        amfReply.addContent(amfData.getContent(1));                               // same transaction ID
        amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); // null - command info
        amfReply.addContent(AMF::Object(""));                            // info
        amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Rejected"));
        amfReply.getContentP(3)->addContent(
            AMF::Object("description", "Play rejected: could not initialize stream"));
        sendCommand(amfReply, messageType, streamId);
        onFinish();
        return;
      }

      // send a status reply
      AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
      amfreply.addContent(AMF::Object("", "onStatus"));          // status reply
      amfreply.addContent(AMF::Object("", playTransaction));     // same transaction ID
      amfreply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfreply.addContent(AMF::Object(""));                      // info
      amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Reset"));
      amfreply.getContentP(3)->addContent(AMF::Object("description", "Playing and resetting..."));
      amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
      amfreply.getContentP(3)->addContent(AMF::Object("clientid", 1337.0));
      sendCommand(amfreply, playMessageType, playStreamId);
      // send streamisrecorded if stream, well, is recorded.
      if (M.getVod()){// isMember("length") && Strm.metadata["length"].asInt() > 0){
        myConn.SendNow(RTMPStream::SendUSR(4, 1)); // send UCM StreamIsRecorded (4), stream 1
      }
      // send streambegin
      myConn.SendNow(RTMPStream::SendUSR(0, 1)); // send UCM StreamBegin (0), stream 1
      // and more reply
      amfreply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
      amfreply.addContent(AMF::Object("", "onStatus"));          // status reply
      amfreply.addContent(AMF::Object("", playTransaction));     // same transaction ID
      amfreply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfreply.addContent(AMF::Object(""));                      // info
      amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Start"));
      amfreply.getContentP(3)->addContent(AMF::Object("description", "Playing!"));
      amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
      amfreply.getContentP(3)->addContent(AMF::Object("clientid", 1337.0));
      initialSeek();
      rtmpOffset = currentTime();
      amfreply.getContentP(3)->addContent(AMF::Object("timecodeOffset", (double)rtmpOffset));
      sendCommand(amfreply, playMessageType, playStreamId);
      RTMPStream::chunk_snd_max = 65536;                                 // 64KiB
      myConn.SendNow(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); // send chunk size max (msg 1)
      // send dunno?
      myConn.SendNow(RTMPStream::SendUSR(32, 1)); // send UCM no clue?, stream 1

      parseData = true;
      return;
    }// play
    if ((amfData.getContentP(0)->StrValue() == "seek")){
      // set reply number and stream name, actual reply is sent up in the ss.spool() handler
      double playTransaction = amfData.getContentP(1)->NumValue();
      int8_t playMessageType = messageType;
      int32_t playStreamId = streamId;

      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "onStatus"));          // status reply
      amfReply.addContent(amfData.getContent(1));                // same transaction ID
      amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfReply.addContent(AMF::Object(""));                      // info
      amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Seek.Notify"));
      amfReply.getContentP(3)->addContent(
          AMF::Object("description", "Seeking to the specified time"));
      amfReply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
      amfReply.getContentP(3)->addContent(AMF::Object("clientid", 1337.0));
      sendCommand(amfReply, playMessageType, playStreamId);
      seek((long long int)amfData.getContentP(3)->NumValue());

      // send a status reply
      AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
      amfreply.addContent(AMF::Object("", "onStatus"));          // status reply
      amfreply.addContent(AMF::Object("", playTransaction));     // same transaction ID
      amfreply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfreply.addContent(AMF::Object(""));                      // info
      amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Reset"));
      amfreply.getContentP(3)->addContent(AMF::Object("description", "Playing and resetting..."));
      amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
      amfreply.getContentP(3)->addContent(AMF::Object("clientid", 1337.0));
      sendCommand(amfreply, playMessageType, playStreamId);
      // send streamisrecorded if stream, well, is recorded.
      if (M.getVod()){// isMember("length") && Strm.metadata["length"].asInt() > 0){
        myConn.SendNow(RTMPStream::SendUSR(4, 1)); // send UCM StreamIsRecorded (4), stream 1
      }
      // send streambegin
      myConn.SendNow(RTMPStream::SendUSR(0, 1)); // send UCM StreamBegin (0), stream 1
      // and more reply
      amfreply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
      amfreply.addContent(AMF::Object("", "onStatus"));          // status reply
      amfreply.addContent(AMF::Object("", playTransaction));     // same transaction ID
      amfreply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
      amfreply.addContent(AMF::Object(""));                      // info
      amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Start"));
      amfreply.getContentP(3)->addContent(AMF::Object("description", "Playing!"));
      amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
      amfreply.getContentP(3)->addContent(AMF::Object("clientid", 1337.0));
      if (M.getLive()){
        rtmpOffset = currentTime();
        amfreply.getContentP(3)->addContent(AMF::Object("timecodeOffset", (double)rtmpOffset));
      }
      sendCommand(amfreply, playMessageType, playStreamId);
      RTMPStream::chunk_snd_max = 65536;                                 // 64KiB
      myConn.SendNow(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); // send chunk size max (msg 1)
      // send dunno?
      myConn.SendNow(RTMPStream::SendUSR(32, 1)); // send UCM no clue?, stream 1

      return;
    }// seek
    if ((amfData.getContentP(0)->StrValue() == "pauseRaw") || (amfData.getContentP(0)->StrValue() == "pause")){
      int8_t playMessageType = messageType;
      int32_t playStreamId = streamId;
      if (amfData.getContentP(3)->NumValue()){
        parseData = false;
        // send a status reply
        AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
        amfReply.addContent(AMF::Object("", "onStatus"));          // status reply
        amfReply.addContent(amfData.getContent(1));                // same transaction ID
        amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
        amfReply.addContent(AMF::Object(""));                      // info
        amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
        amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Pause.Notify"));
        amfReply.getContentP(3)->addContent(AMF::Object("description", "Pausing playback"));
        amfReply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
        amfReply.getContentP(3)->addContent(AMF::Object("clientid", 1337.0));
        sendCommand(amfReply, playMessageType, playStreamId);
      }else{
        parseData = true;
        // send a status reply
        AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
        amfReply.addContent(AMF::Object("", "onStatus"));          // status reply
        amfReply.addContent(amfData.getContent(1));                // same transaction ID
        amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // null - command info
        amfReply.addContent(AMF::Object(""));                      // info
        amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
        amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Unpause.Notify"));
        amfReply.getContentP(3)->addContent(AMF::Object("description", "Resuming playback"));
        amfReply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
        amfReply.getContentP(3)->addContent(AMF::Object("clientid", 1337.0));
        sendCommand(amfReply, playMessageType, playStreamId);
      }
      return;
    }// seek
    if (amfData.getContentP(0)->StrValue() == "_error"){
      if (!amfData.getContentP(3)){
        WARN_MSG("Received generic error response (no useful content)");
        return;
      }
      if (amfData.getContentP(3)->GetType() == AMF::AMF0_OBJECT){
        std::string code, description;
        if (amfData.getContentP(3)->getContentP("code") &&
            amfData.getContentP(3)->getContentP("code")->StrValue().size()){
          code = amfData.getContentP(3)->getContentP("code")->StrValue();
        }
        if (amfData.getContentP(3)->getContentP("description") &&
            amfData.getContentP(3)->getContentP("description")->StrValue().size()){
          description = amfData.getContentP(3)->getContentP("description")->StrValue();
        }
        if (amfData.getContentP(3)->getContentP("details") &&
            amfData.getContentP(3)->getContentP("details")->StrValue().size()){
          if (description.size()){
            description += "," + amfData.getContentP(3)->getContentP("details")->StrValue();
          }else{
            description = amfData.getContentP(3)->getContentP("details")->StrValue();
          }
        }
        if (code.size() || description.size()){
          if (description.find("authmod=adobe") != std::string::npos){
            if (!pushUrl.user.size() && !pushUrl.pass.size()){
              FAIL_MSG("Receiving side wants credentials, but none were provided in the target");
              return;
            }
            if (description.find("?reason=authfailed") != std::string::npos || authAttempts > 1){
              FAIL_MSG(
                  "Credentials provided in the target were not accepted by the receiving side");
              myConn.close();
              return;
            }
            if (description.find("?reason=needauth") != std::string::npos){
              std::map<std::string, std::string> authVars;
              HTTP::parseVars(description.substr(description.find("?reason=needauth") + 1), authVars);
              std::string authSalt = authVars.count("salt") ? authVars["salt"] : "";
              std::string authOpaque = authVars.count("opaque") ? authVars["opaque"] : "";
              std::string authChallenge = authVars.count("challenge") ? authVars["challenge"] : "";
              std::string authNonce = authVars.count("nonce") ? authVars["nonce"] : "";
              INFO_MSG("Adobe auth: sending credentials phase 2 (salt=%s, opaque=%s, challenge=%s, "
                       "nonce=%s)",
                       authSalt.c_str(), authOpaque.c_str(), authChallenge.c_str(), authNonce.c_str());
              authAttempts++;

              char md5buffer[16];
              std::string to_hash = pushUrl.user + authSalt + pushUrl.pass;
              Secure::md5bin(to_hash.data(), to_hash.size(), md5buffer);
              std::string hash_one = Encodings::Base64::encode(std::string(md5buffer, 16));
              if (authOpaque.size()){
                to_hash = hash_one + authOpaque + "00000000";
              }else if (authChallenge.size()){
                to_hash = hash_one + authChallenge + "00000000";
              }
              Secure::md5bin(to_hash.data(), to_hash.size(), md5buffer);
              std::string hash_two = Encodings::Base64::encode(std::string(md5buffer, 16));
              std::string authStr = "?authmod=adobe&user=" + Encodings::URL::encode(pushUrl.user, "/:=@[]") +
                                    "&challenge=00000000&response=" + hash_two;
              if (authOpaque.size()){authStr += "&opaque=" + Encodings::URL::encode(authOpaque, "/:=@[]");}
              startPushOut(authStr.c_str());
              return;
            }
            INFO_MSG("Adobe auth: sending credentials phase 1");
            authAttempts++;
            std::string authStr = "?authmod=adobe&user=" + Encodings::URL::encode(pushUrl.user, "/:=@[]");
            startPushOut(authStr.c_str());
            return;
          }
          WARN_MSG("Received error response: %s; %s",
                   amfData.getContentP(3)->getContentP("code")->StrValue().c_str(),
                   amfData.getContentP(3)->getContentP("description")->StrValue().c_str());
        }else{
          WARN_MSG("Received generic error response (no useful content)");
        }
        return;
      }
      if (amfData.getContentP(3)->GetType() == AMF::AMF0_STRING){
        WARN_MSG("Received error response: %s", amfData.getContentP(3)->StrValue().c_str());
        return;
      }
      WARN_MSG("Received error response: %s", amfData.Print().c_str());
      return;
    }
    if ((amfData.getContentP(0)->StrValue() == "_result") || (amfData.getContentP(0)->StrValue() == "onFCPublish") ||
        (amfData.getContentP(0)->StrValue() == "onStatus")){
      if (isRecording() && amfData.getContentP(0)->StrValue() == "_result" &&
          amfData.getContentP(1)->NumValue() == 1){
        {
          AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
          amfReply.addContent(AMF::Object("", "releaseStream"));     // command
          amfReply.addContent(AMF::Object("", 2.0));                 // transaction ID
          amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // options
          amfReply.addContent(AMF::Object("", streamOut));           // stream name
          sendCommand(amfReply, 20, 0);
        }
        {
          AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
          amfReply.addContent(AMF::Object("", "FCPublish"));         // command
          amfReply.addContent(AMF::Object("", 3.0));                 // transaction ID
          amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // options
          amfReply.addContent(AMF::Object("", streamOut));           // stream name
          sendCommand(amfReply, 20, 0);
        }
        {
          AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
          amfReply.addContent(AMF::Object("", "createStream"));      // command
          amfReply.addContent(AMF::Object("", 4.0));                 // transaction ID
          amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // options
          sendCommand(amfReply, 20, 0);
        }
        {
          AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
          amfReply.addContent(AMF::Object("", "publish"));           // command
          amfReply.addContent(AMF::Object("", 5.0));                 // transaction ID
          amfReply.addContent(AMF::Object("", 0.0, AMF::AMF0_NULL)); // options
          amfReply.addContent(AMF::Object("", streamOut));           // stream name
          amfReply.addContent(AMF::Object("", "live"));              // stream name
          sendCommand(amfReply, 20, 1);
        }
        HIGH_MSG("Publish starting");
        if (!targetParams.count("realtime")){realTime = 0;}
        parseData = true;
        return;
      }
      if (amfData.getContentP(0)->StrValue() == "onStatus" &&
          amfData.getContentP(3)->getContentP("level")->StrValue() == "error"){
        WARN_MSG("Received error response: %s; %s",
                 amfData.getContentP(3)->getContentP("code")->StrValue().c_str(),
                 amfData.getContentP(3)->getContentP("description")->StrValue().c_str());
        return;
      }

      // Other results are ignored. We don't really care.
      return;
    }

    WARN_MSG("AMF0 command not processed: %s", amfData.Print().c_str());
    // send a _result reply
    AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
    amfReply.addContent(AMF::Object("", "_error"));                           // result success
    amfReply.addContent(amfData.getContent(1));                               // same transaction ID
    amfReply.addContent(AMF::Object("", amfData.getContentP(0)->StrValue())); // null - command info
    amfReply.addContent(AMF::Object("", "Command not implemented or recognized")); // stream ID?
    sendCommand(amfReply, messageType, streamId);
  }// parseAMFCommand

  ///\brief Gets and parses one RTMP chunk at a time.
  ///\param inputBuffer A buffer filled with chunk data.
  void OutRTMP::parseChunk(Socket::Buffer &inputBuffer){
    // for DTSC conversion
    static std::stringstream prebuffer; // Temporary buffer before sending real data
    // for chunk parsing
    static RTMPStream::Chunk next;
    static FLV::Tag F;
    static AMF::Object amfdata("empty", AMF::AMF0_DDV_CONTAINER);
    static AMF::Object amfelem("empty", AMF::AMF0_DDV_CONTAINER);
    static AMF::Object3 amf3data("empty", AMF::AMF3_DDV_CONTAINER);
    static AMF::Object3 amf3elem("empty", AMF::AMF3_DDV_CONTAINER);

    while (next.Parse(inputBuffer)){

      // send ACK if we received a whole window
      if ((RTMPStream::rec_cnt - RTMPStream::rec_window_at > RTMPStream::rec_window_size / 4) || Util::bootSecs() > lastAck+15){
        lastAck = Util::bootSecs();
        RTMPStream::rec_window_at = RTMPStream::rec_cnt;
        myConn.SendNow(RTMPStream::SendCTL(3, RTMPStream::rec_cnt)); // send ack (msg 3)
      }

      switch (next.msg_type_id){
      case 0: // does not exist
        WARN_MSG("UNKN: Received a zero-type message. Possible data corruption? Aborting!");
        while (inputBuffer.size()){inputBuffer.get().clear();}
        stop();
        onFinish();
        break; // happens when connection breaks unexpectedly
      case 1:  // set chunk size
        RTMPStream::chunk_rec_max = Bit::btohl(next.data.data());
        MEDIUM_MSG("CTRL: Set chunk size: %zu", RTMPStream::chunk_rec_max);
        break;
      case 2: // abort message - we ignore this one
        MEDIUM_MSG("CTRL: Abort message");
        // 4 bytes of stream id to drop
        break;
      case 3: // ack
        VERYHIGH_MSG("CTRL: Acknowledgement");
        RTMPStream::snd_window_at = Bit::btohl(next.data.data());
        RTMPStream::snd_window_at = RTMPStream::snd_cnt;
        break;
      case 4:{
        // 2 bytes event type, rest = event data
        // types:
        // 0 = stream begin, 4 bytes ID
        // 1 = stream EOF, 4 bytes ID
        // 2 = stream dry, 4 bytes ID
        // 3 = setbufferlen, 4 bytes ID, 4 bytes length
        // 4 = streamisrecorded, 4 bytes ID
        // 6 = pingrequest, 4 bytes data
        // 7 = pingresponse, 4 bytes data
        // we don't need to process this
        int16_t ucmtype = Bit::btohs(next.data.data());
        switch (ucmtype){
        case 0:
          MEDIUM_MSG("CTRL: UCM StreamBegin %" PRIu32, Bit::btohl(next.data.data() + 2));
          break;
        case 1: MEDIUM_MSG("CTRL: UCM StreamEOF %" PRIu32, Bit::btohl(next.data.data() + 2)); break;
        case 2: MEDIUM_MSG("CTRL: UCM StreamDry %" PRIu32, Bit::btohl(next.data.data() + 2)); break;
        case 3:
          MEDIUM_MSG("CTRL: UCM SetBufferLength %" PRIu32 " %" PRIu32,
                     Bit::btohl(next.data.data() + 2), Bit::btohl(next.data.data() + 6));
          break;
        case 4:
          MEDIUM_MSG("CTRL: UCM StreamIsRecorded %" PRIu32, Bit::btohl(next.data.data() + 2));
          break;
        case 6:
          MEDIUM_MSG("CTRL: UCM PingRequest %" PRIu32, Bit::btohl(next.data.data() + 2));
          myConn.SendNow(RTMPStream::SendUSR(7, Bit::btohl(next.data.data() + 2))); // send UCM PingResponse (7)
          break;
        case 7:
          MEDIUM_MSG("CTRL: UCM PingResponse %" PRIu32, Bit::btohl(next.data.data() + 2));
          break;
        default: MEDIUM_MSG("CTRL: UCM Unknown (%" PRId16 ")", ucmtype); break;
        }
      }break;
      case 5: // window size of other end
        MEDIUM_MSG("CTRL: Window size");
        RTMPStream::rec_window_size = Bit::btohl(next.data.data());
        RTMPStream::rec_window_at = RTMPStream::rec_cnt;
        myConn.SendNow(RTMPStream::SendCTL(3, RTMPStream::rec_cnt)); // send ack (msg 3)
        lastAck = Util::bootSecs();
        break;
      case 6:
        MEDIUM_MSG("CTRL: Set peer bandwidth");
        // 4 bytes window size, 1 byte limit type (ignored)
        RTMPStream::snd_window_size = Bit::btohl(next.data.data());
        myConn.SendNow(RTMPStream::SendCTL(5, RTMPStream::snd_window_size)); // send window acknowledgement size (msg 5)
        break;
      case 8:    // audio data
      case 9:    // video data
      case 18:{// meta data
        static std::map<size_t, AMF::Object> pushMeta;
        static std::map<size_t, uint64_t> lastTagTime;
        static std::map<size_t, int64_t> trackOffset;
        static std::map<size_t, size_t> reTrackToID;
        if (!isInitialized || !meta){
          MEDIUM_MSG("Received useless media data");
          onFinish();
          break;
        }
        F.ChunkLoader(next);
        if (!F.getDataLen()){break;}// ignore empty packets
        AMF::Object *amf_storage = 0;
        if (F.data[0] == 0x12 || pushMeta.count(next.cs_id) || !pushMeta.size()){
          amf_storage = &(pushMeta[next.cs_id]);
        }else{
          amf_storage = &(pushMeta.begin()->second);
        }

        size_t reTrack = next.cs_id * 3 + (F.data[0] == 0x09 ? 1 : (F.data[0] == 0x08 ? 2 : 3));
        if (!reTrackToID.count(reTrack)){reTrackToID[reTrack] = INVALID_TRACK_ID;}
        F.toMeta(meta, *amf_storage, reTrackToID[reTrack], targetParams);
        if (F.getDataLen() && !(F.needsInitData() && F.isInitData())){
          uint64_t tagTime = next.timestamp;
          uint64_t timeOffset = 0;
          if (targetParams.count("timeoffset")){
            timeOffset = JSON::Value(targetParams["timeoffset"]).asInt();
          }
          if (!M.getBootMsOffset()){
            meta.setBootMsOffset(Util::bootMS() - tagTime);
            rtmpOffset = timeOffset;
            setRtmpOffset = true;
          }else if (!setRtmpOffset){
            rtmpOffset = (Util::bootMS() - tagTime) - M.getBootMsOffset() + timeOffset;
            setRtmpOffset = true;
          }
          tagTime += rtmpOffset + trackOffset[reTrack];
          uint64_t &ltt = lastTagTime[reTrack];
          if (tagTime < ltt){
            uint64_t diff = ltt - tagTime;
            // Round to 24-bit rollover if within 0xfff of it on either side.
            // Round to 32-bit rollover if within 0xfff of it on either side.
            // Make sure time increases by 1ms if neither applies.
            if (diff > 0xfff000ull && diff < 0x1000fffull){
              diff = 0x1000000ull;
              WARN_MSG("Timestamp for %s went from %" PRIu64 " to %" PRIu64 " (decreased by 24-bit rollover): compensating", trackType(next.msg_type_id), ltt, tagTime);
            }else if (diff > 0xfffff000ull && diff < 0x100000fffull){
              diff = 0x100000000ull;
              WARN_MSG("Timestamp for %s went from %" PRIu64 " to %" PRIu64 " (decreased by 32-bit rollover): compensating", trackType(next.msg_type_id), ltt, tagTime);
            }else{
              diff += 1;
              WARN_MSG("Timestamp for %s went from %" PRIu64 " to %" PRIu64 " (decreased by %" PRIu64 "): compensating", trackType(next.msg_type_id), ltt, tagTime, diff);
            }
            trackOffset[reTrack] += diff;
            tagTime += diff;
          }else if (tagTime > ltt + 600000){
            uint64_t diff = tagTime - ltt;
            // Round to 24-bit rollover if within 0xfff of it on either side.
            // Round to 32-bit rollover if within 0xfff of it on either side.
            // Make sure time increases by 1ms if neither applies.
            if (diff > 0xfff000ull && diff < 0x1000fffull){
              diff = 0x1000000ull;
              WARN_MSG("Timestamp for %s went from %" PRIu64 " to %" PRIu64 " (increased by 24-bit rollover): compensating", trackType(next.msg_type_id), ltt, tagTime);
            }else if (diff > 0xfffff000ull && diff < 0x100000fffull){
              diff = 0x100000000ull;
              WARN_MSG("Timestamp for %s went from %" PRIu64 " to %" PRIu64 " (increased by 32-bit rollover): compensating", trackType(next.msg_type_id), ltt, tagTime);
            }else{
              diff -= 1;
              if (ltt){
                WARN_MSG("Timestamp for %s went from %" PRIu64 " to %" PRIu64 " (increased by %" PRIu64 "): compensating", trackType(next.msg_type_id), ltt, tagTime, diff);
              }
            }
            if (ltt){
              trackOffset[reTrack] -= diff;
            }else{
              rtmpOffset -= diff;
            }
            tagTime -= diff;
          }
          size_t idx = reTrackToID[reTrack];
          if (idx != INVALID_TRACK_ID && !userSelect.count(idx)){
            userSelect[idx].reload(streamName, idx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE);
          }
          if (M.getCodec(idx) == "PCM" && M.getSize(idx) == 16){
            char *ptr = F.getData();
            uint32_t ptrSize = F.getDataLen();
            for (uint32_t i = 0; i < ptrSize; i += 2){
              char tmpchar = ptr[i];
              ptr[i] = ptr[i + 1];
              ptr[i + 1] = tmpchar;
            }
          }
          ltt = tagTime;
          //            bufferLivePacket(thisPacket);
          bufferLivePacket(tagTime, F.offset(), idx, F.getData(), F.getDataLen(), 0, F.isKeyframe);
          if (!meta){config->is_active = false;}
        }
        break;
      }
      case 15: MEDIUM_MSG("Received AMF3 data message"); break;
      case 16: MEDIUM_MSG("Received AMF3 shared object"); break;
      case 17:{
        MEDIUM_MSG("Received AMF3 command message");
        if (next.data[0] != 0){
          next.data = next.data.substr(1);
          amf3data = AMF::parse3(next.data);
          MEDIUM_MSG("AMF3: %s", amf3data.Print().c_str());
        }else{
          MEDIUM_MSG("Received AMF3-0 command message");
          next.data = next.data.substr(1);
          amfdata = AMF::parse(next.data);
          parseAMFCommand(amfdata, 17, next.msg_stream_id);
        }// parsing AMF0-style
      }break;
      case 19: MEDIUM_MSG("Received AMF0 shared object"); break;
      case 20:{// AMF0 command message
        amfdata = AMF::parse(next.data);
        parseAMFCommand(amfdata, 20, next.msg_stream_id);
      }break;
      case 22: MEDIUM_MSG("Received aggregate message"); break;
      default:
        FAIL_MSG("Unknown chunk received! Probably protocol corruption, stopping parsing of "
                 "incoming data.");
        break;
      }
    }
  }
}// namespace Mist
