/// \file conn_rtmp.cpp
/// Contains the main code for the RTMP Connector

#include <iostream>
#include <sstream>

#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <getopt.h>

#include <mist/socket.h>
#include <mist/config.h>
#include <mist/flv_tag.h>
#include <mist/amf.h>
#include <mist/rtmpchunks.h>
#include <mist/stream.h>
#include <mist/timing.h>

///\brief Holds everything unique to the RTMP Connector
namespace Connector_RTMP {

  //for connection to server
  bool ready4data = false; ///< Indicates whether streaming can start.
  bool inited = false; ///< Indicates whether we are ready to connect to the Buffer.
  bool noStats = false; ///< Indicates when no stats should be sent anymore. Used in push mode.
  bool stopParsing = false; ///< Indicates when to stop all parsing.

  //for reply to play command
  int playTransaction = -1;///<The transaction number of the reply.
  int playStreamId = -1;///<The stream id of the reply.
  int playMessageType = -1;///<The message type of the reply.

  //generic state keeping
  bool streamInited = false;///<Indicates whether init data for audio/video was sent.
  
  Socket::Connection Socket; ///< A copy of the user socket to allow helper functions to directly send data.
  Socket::Connection ss; ///< Socket connected to server.
  std::string streamName; ///< Stream that will be opened.

  ///\brief Sends a RTMP command either in AMF or AMF3 mode.
  ///\param amfReply The data to be sent over RTMP.
  ///\param messageType The type of message.
  ///\param streamId The ID of the AMF stream.
  void sendCommand(AMF::Object & amfReply, int messageType, int streamId){
  #if DEBUG >= 8
    std::cerr << amfReply.Print() << std::endl;
  #endif
    if (messageType == 17){
      Socket.SendNow(RTMPStream::SendChunk(3, messageType, streamId, (char)0 + amfReply.Pack()));
    }else{
      Socket.SendNow(RTMPStream::SendChunk(3, messageType, streamId, amfReply.Pack()));
    }
  } //sendCommand

  ///\brief Parses a single AMF command message, and sends a direct response through sendCommand().
  ///\param amfData The received request.
  ///\param messageType The type of message.
  ///\param streamId The ID of the AMF stream.
  void parseAMFCommand(AMF::Object & amfData, int messageType, int streamId){
  #if DEBUG >= 5
    fprintf(stderr, "Received command: %s\n", amfData.Print().c_str());
  #endif
  #if DEBUG >= 8
    fprintf(stderr, "AMF0 command: %s\n", amfData.getContentP(0)->StrValue().c_str());
  #endif
    if (amfData.getContentP(0)->StrValue() == "connect"){
      double objencoding = 0;
      if (amfData.getContentP(2)->getContentP("objectEncoding")){
        objencoding = amfData.getContentP(2)->getContentP("objectEncoding")->NumValue();
      }
  #if DEBUG >= 6
      int tmpint;
      if (amfData.getContentP(2)->getContentP("videoCodecs")){
        tmpint = (int)amfData.getContentP(2)->getContentP("videoCodecs")->NumValue();
        if (tmpint & 0x04){
          fprintf(stderr, "Sorensen video support detected\n");
        }
        if (tmpint & 0x80){
          fprintf(stderr, "H264 video support detected\n");
        }
      }
      if (amfData.getContentP(2)->getContentP("audioCodecs")){
        tmpint = (int)amfData.getContentP(2)->getContentP("audioCodecs")->NumValue();
        if (tmpint & 0x04){
          fprintf(stderr, "MP3 audio support detected\n");
        }
        if (tmpint & 0x400){
          fprintf(stderr, "AAC audio support detected\n");
        }
      }
  #endif
      RTMPStream::chunk_snd_max = 4096;
      Socket.Send(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); //send chunk size max (msg 1)
      Socket.Send(RTMPStream::SendCTL(5, RTMPStream::snd_window_size)); //send window acknowledgement size (msg 5)
      Socket.Send(RTMPStream::SendCTL(6, RTMPStream::rec_window_size)); //send rec window acknowledgement size (msg 6)
      Socket.Send(RTMPStream::SendUSR(0, 1)); //send UCM StreamBegin (0), stream 1
      //send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("")); //server properties
      amfReply.getContentP(2)->addContent(AMF::Object("fmsVer", "FMS/3,5,5,2004"));
      amfReply.getContentP(2)->addContent(AMF::Object("capabilities", (double)31));
      amfReply.getContentP(2)->addContent(AMF::Object("mode", (double)1));
      amfReply.addContent(AMF::Object("")); //info
      amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfReply.getContentP(3)->addContent(AMF::Object("code", "NetConnection.Connect.Success"));
      amfReply.getContentP(3)->addContent(AMF::Object("description", "Connection succeeded."));
      amfReply.getContentP(3)->addContent(AMF::Object("clientid", 1337));
      amfReply.getContentP(3)->addContent(AMF::Object("objectEncoding", objencoding));
      //amfReply.getContentP(3)->addContent(AMF::Object("data", AMF::AMF0_ECMA_ARRAY));
      //amfReply.getContentP(3)->getContentP(4)->addContent(AMF::Object("version", "3,5,4,1004"));
      sendCommand(amfReply, messageType, streamId);
      //send onBWDone packet - no clue what it is, but real server sends it...
      //amfReply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
      //amfReply.addContent(AMF::Object("", "onBWDone"));//result
      //amfReply.addContent(amfData.getContent(1));//same transaction ID
      //amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null
      //sendCommand(amfReply, messageType, streamId);
      return;
    } //connect
    if (amfData.getContentP(0)->StrValue() == "createStream"){
      //send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("", (double)1)); //stream ID - we use 1
      sendCommand(amfReply, messageType, streamId);
      Socket.Send(RTMPStream::SendUSR(0, 1)); //send UCM StreamBegin (0), stream 1
      return;
    } //createStream
    if ((amfData.getContentP(0)->StrValue() == "closeStream") || (amfData.getContentP(0)->StrValue() == "deleteStream")){
      if (ss.connected()){
        ss.close();
      }
      return;
    }
    if ((amfData.getContentP(0)->StrValue() == "FCUnpublish") || (amfData.getContentP(0)->StrValue() == "releaseStream")){
      // ignored
      return;
    }
    if ((amfData.getContentP(0)->StrValue() == "FCPublish")){
      //send a FCPublic reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "onFCPublish")); //status reply
      amfReply.addContent(AMF::Object("", 0, AMF::AMF0_NUMBER)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("")); //info
      amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Publish.Start"));
      amfReply.getContentP(3)->addContent(AMF::Object("description", "Please followup with publish command..."));
      sendCommand(amfReply, messageType, streamId);
      return;
    } //FCPublish
    if (amfData.getContentP(0)->StrValue() == "releaseStream"){
      //send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("", AMF::AMF0_UNDEFINED)); //stream ID?
      sendCommand(amfReply, messageType, streamId);
      return;
    }//releaseStream
    if ((amfData.getContentP(0)->StrValue() == "getStreamLength") || (amfData.getContentP(0)->StrValue() == "getMovLen")){
      //send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("", (double)0)); //zero length
      sendCommand(amfReply, messageType, streamId);
      return;
    } //getStreamLength
    if ((amfData.getContentP(0)->StrValue() == "publish")){
      if (amfData.getContentP(3)){
        streamName = amfData.getContentP(3)->StrValue();
        /// \todo implement push for MistPlayer or restrict and change to getLive
        ss = Util::Stream::getStream(streamName);
        if ( !ss.connected()){
  #if DEBUG >= 1
          fprintf(stderr, "Could not connect to server!\n");
  #endif
          Socket.close(); //disconnect user
          return;
        }
        ss.Send("P ");
        ss.Send(Socket.getHost().c_str());
        ss.Send("\n");
        noStats = true;
      }
      //send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("", 1, AMF::AMF0_BOOL)); //publish success?
      sendCommand(amfReply, messageType, streamId);
      Socket.Send(RTMPStream::SendUSR(0, 1)); //send UCM StreamBegin (0), stream 1
      //send a status reply
      amfReply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "onStatus")); //status reply
      amfReply.addContent(AMF::Object("", 0, AMF::AMF0_NUMBER)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("")); //info
      amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Publish.Start"));
      amfReply.getContentP(3)->addContent(AMF::Object("description", "Stream is now published!"));
      amfReply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
      sendCommand(amfReply, messageType, streamId);
      return;
    } //getStreamLength
    if (amfData.getContentP(0)->StrValue() == "checkBandwidth"){
      //send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      sendCommand(amfReply, messageType, streamId);
      return;
    } //checkBandwidth
    if ((amfData.getContentP(0)->StrValue() == "play") || (amfData.getContentP(0)->StrValue() == "play2")){
      //set reply number and stream name, actual reply is sent up in the ss.spool() handler
      playTransaction = amfData.getContentP(1)->NumValue();
      playMessageType = messageType;
      playStreamId = streamId;
      streamName = amfData.getContentP(3)->StrValue();
      Connector_RTMP::ready4data = true; //start sending video data!
      return;
    } //play
    if ((amfData.getContentP(0)->StrValue() == "seek")){
      //set reply number and stream name, actual reply is sent up in the ss.spool() handler
      playTransaction = amfData.getContentP(1)->NumValue();
      playMessageType = messageType;
      playStreamId = streamId;
      streamInited = false;

      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "onStatus")); //status reply
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("")); //info
      amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Seek.Notify"));
      amfReply.getContentP(3)->addContent(AMF::Object("description", "Seeking to the specified time"));
      amfReply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
      amfReply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
      sendCommand(amfReply, playMessageType, playStreamId);
      ss.Send("s ");
      ss.Send(JSON::Value((long long int)amfData.getContentP(3)->NumValue()).asString().c_str());
      ss.Send("\n");
      return;
    } //seek
    if ((amfData.getContentP(0)->StrValue() == "pauseRaw") || (amfData.getContentP(0)->StrValue() == "pause")){
      if (amfData.getContentP(3)->NumValue()){
        ss.Send("q\n"); //quit playing
        //send a status reply
        AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
        amfReply.addContent(AMF::Object("", "onStatus")); //status reply
        amfReply.addContent(amfData.getContent(1)); //same transaction ID
        amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
        amfReply.addContent(AMF::Object("")); //info
        amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
        amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Pause.Notify"));
        amfReply.getContentP(3)->addContent(AMF::Object("description", "Pausing playback"));
        amfReply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
        amfReply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
        sendCommand(amfReply, playMessageType, playStreamId);
      }else{
        ss.Send("p\n"); //start playing
        //send a status reply
        AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
        amfReply.addContent(AMF::Object("", "onStatus")); //status reply
        amfReply.addContent(amfData.getContent(1)); //same transaction ID
        amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
        amfReply.addContent(AMF::Object("")); //info
        amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
        amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Unpause.Notify"));
        amfReply.getContentP(3)->addContent(AMF::Object("description", "Resuming playback"));
        amfReply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
        amfReply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
        sendCommand(amfReply, playMessageType, playStreamId);
      }
      return;
    } //seek

  #if DEBUG >= 2
    fprintf(stderr, "AMF0 command not processed!\n%s\n", amfData.Print().c_str());
  #endif
  } //parseAMFCommand
  
  ///\brief Gets and parses one RTMP chunk at a time.
  ///\param inputBuffer A buffer filled with chunk data.
  void parseChunk(Socket::Buffer & inputBuffer){
    //for DTSC conversion
    static JSON::Value meta_out;
    static std::stringstream prebuffer; // Temporary buffer before sending real data
    static bool sending = false;
    static unsigned int counter = 0;
    //for chunk parsing
    static RTMPStream::Chunk next;
    static FLV::Tag F;
    static AMF::Object amfdata("empty", AMF::AMF0_DDV_CONTAINER);
    static AMF::Object amfelem("empty", AMF::AMF0_DDV_CONTAINER);
    static AMF::Object3 amf3data("empty", AMF::AMF3_DDV_CONTAINER);
    static AMF::Object3 amf3elem("empty", AMF::AMF3_DDV_CONTAINER);

    while (next.Parse(inputBuffer)){

      //send ACK if we received a whole window
      if ((RTMPStream::rec_cnt - RTMPStream::rec_window_at > RTMPStream::rec_window_size)){
        RTMPStream::rec_window_at = RTMPStream::rec_cnt;
        Socket.Send(RTMPStream::SendCTL(3, RTMPStream::rec_cnt)); //send ack (msg 3)
      }

      switch (next.msg_type_id){
        case 0: //does not exist
  #if DEBUG >= 2
          fprintf(stderr, "UNKN: Received a zero-type message. Possible data corruption? Aborting!\n");
  #endif
          while (inputBuffer.size()){
            inputBuffer.get().clear();
          }
          ss.close();
          Socket.close();
          break; //happens when connection breaks unexpectedly
        case 1: //set chunk size
          RTMPStream::chunk_rec_max = ntohl(*(int*)next.data.c_str());
  #if DEBUG >= 5
          fprintf(stderr, "CTRL: Set chunk size: %i\n", RTMPStream::chunk_rec_max);
  #endif
          break;
        case 2: //abort message - we ignore this one
  #if DEBUG >= 5
          fprintf(stderr, "CTRL: Abort message\n");
  #endif
          //4 bytes of stream id to drop
          break;
        case 3: //ack
  #if DEBUG >= 8
          fprintf(stderr, "CTRL: Acknowledgement\n");
  #endif
          RTMPStream::snd_window_at = ntohl(*(int*)next.data.c_str());
          RTMPStream::snd_window_at = RTMPStream::snd_cnt;
          break;
        case 4: {
          //2 bytes event type, rest = event data
          //types:
          //0 = stream begin, 4 bytes ID
          //1 = stream EOF, 4 bytes ID
          //2 = stream dry, 4 bytes ID
          //3 = setbufferlen, 4 bytes ID, 4 bytes length
          //4 = streamisrecorded, 4 bytes ID
          //6 = pingrequest, 4 bytes data
          //7 = pingresponse, 4 bytes data
          //we don't need to process this
  #if DEBUG >= 5
          short int ucmtype = ntohs(*(short int*)next.data.c_str());
          switch (ucmtype){
            case 0:
              fprintf(stderr, "CTRL: UCM StreamBegin %i\n", ntohl(*((int*)(next.data.c_str()+2))));
              break;
            case 1:
              fprintf(stderr, "CTRL: UCM StreamEOF %i\n", ntohl(*((int*)(next.data.c_str()+2))));
              break;
            case 2:
              fprintf(stderr, "CTRL: UCM StreamDry %i\n", ntohl(*((int*)(next.data.c_str()+2))));
              break;
            case 3:
              fprintf(stderr, "CTRL: UCM SetBufferLength %i %i\n", ntohl(*((int*)(next.data.c_str()+2))), ntohl(*((int*)(next.data.c_str()+6))));
              break;
            case 4:
              fprintf(stderr, "CTRL: UCM StreamIsRecorded %i\n", ntohl(*((int*)(next.data.c_str()+2))));
              break;
            case 6:
              fprintf(stderr, "CTRL: UCM PingRequest %i\n", ntohl(*((int*)(next.data.c_str()+2))));
              break;
            case 7:
              fprintf(stderr, "CTRL: UCM PingResponse %i\n", ntohl(*((int*)(next.data.c_str()+2))));
              break;
            default:
              fprintf(stderr, "CTRL: UCM Unknown (%hi)\n", ucmtype);
              break;
          }
  #endif
        }
          break;
        case 5: //window size of other end
  #if DEBUG >= 5
          fprintf(stderr, "CTRL: Window size\n");
  #endif
          RTMPStream::rec_window_size = ntohl(*(int*)next.data.c_str());
          RTMPStream::rec_window_at = RTMPStream::rec_cnt;
          Socket.Send(RTMPStream::SendCTL(3, RTMPStream::rec_cnt)); //send ack (msg 3)
          break;
        case 6:
  #if DEBUG >= 5
          fprintf(stderr, "CTRL: Set peer bandwidth\n");
  #endif
          //4 bytes window size, 1 byte limit type (ignored)
          RTMPStream::snd_window_size = ntohl(*(int*)next.data.c_str());
          Socket.Send(RTMPStream::SendCTL(5, RTMPStream::snd_window_size)); //send window acknowledgement size (msg 5)
          break;
        case 8: //audio data
        case 9: //video data
        case 18: //meta data
          if (ss.connected()){
            F.ChunkLoader(next);
            JSON::Value pack_out = F.toJSON(meta_out);
            if ( !pack_out.isNull()){
              if ( !sending){
                counter++;
                if (counter > 8){
                  sending = true;
                  ss.SendNow(meta_out.toNetPacked());
                  ss.SendNow(prebuffer.str().c_str(), prebuffer.str().size()); //write buffer
                  prebuffer.str(""); //clear buffer
                  ss.SendNow(pack_out.toNetPacked());
                }else{
                  prebuffer << pack_out.toNetPacked();
                }
              }else{
                ss.SendNow(pack_out.toNetPacked());
              }
            }
          }else{
  #if DEBUG >= 5
            fprintf(stderr, "Received useless media data\n");
  #endif
            Socket.close();
          }
          break;
        case 15:
  #if DEBUG >= 5
          fprintf(stderr, "Received AFM3 data message\n");
  #endif
          break;
        case 16:
  #if DEBUG >= 5
          fprintf(stderr, "Received AFM3 shared object\n");
  #endif
          break;
        case 17: {
  #if DEBUG >= 5
          fprintf(stderr, "Received AFM3 command message\n");
  #endif
          if (next.data[0] != 0){
            next.data = next.data.substr(1);
            amf3data = AMF::parse3(next.data);
  #if DEBUG >= 5
            amf3data.Print();
  #endif
          }else{
  #if DEBUG >= 5
            fprintf(stderr, "Received AFM3-0 command message\n");
  #endif
            next.data = next.data.substr(1);
            amfdata = AMF::parse(next.data);
            parseAMFCommand(amfdata, 17, next.msg_stream_id);
          } //parsing AMF0-style
        }
          break;
        case 19:
  #if DEBUG >= 5
          fprintf(stderr, "Received AFM0 shared object\n");
  #endif
          break;
        case 20: { //AMF0 command message
          amfdata = AMF::parse(next.data);
          parseAMFCommand(amfdata, 20, next.msg_stream_id);
        }
          break;
        case 22:
  #if DEBUG >= 5
          fprintf(stderr, "Received aggregate message\n");
  #endif
          break;
        default:
  #if DEBUG >= 1
          fprintf(stderr, "Unknown chunk received! Probably protocol corruption, stopping parsing of incoming data.\n");
  #endif
          stopParsing = true;
          break;
      }
    }
  } //parseChunk

  ///\brief Main function for the RTMP Connector
  ///\param conn A socket describing the connection the client.
  ///\return The exit code of the connector.
  int rtmpConnector(Socket::Connection conn){
    Socket = conn;
    Socket.setBlocking(false);
    FLV::Tag tag, init_tag;
    DTSC::Stream Strm;

    while ( !Socket.Received().available(1537) && Socket.connected()){
      Socket.spool();
      Util::sleep(5);
    }
    RTMPStream::handshake_in = Socket.Received().remove(1537);
    RTMPStream::rec_cnt += 1537;

    if (RTMPStream::doHandshake()){
      Socket.SendNow(RTMPStream::handshake_out);
      while ( !Socket.Received().available(1536) && Socket.connected()){
        Socket.spool();
        Util::sleep(5);
      }
      Socket.Received().remove(1536);
      RTMPStream::rec_cnt += 1536;
  #if DEBUG >= 5
      fprintf(stderr, "Handshake succcess!\n");
  #endif
    }else{
  #if DEBUG >= 5
      fprintf(stderr, "Handshake fail!\n");
  #endif
      return 0;
    }

    unsigned int lastStats = 0;
    bool firsttime = true;

    while (Socket.connected()){
      if (Socket.spool() || firsttime){
        parseChunk(Socket.Received());
        firsttime = false;
      }else{
        Util::sleep(1); //sleep 1ms to prevent high CPU usage
      }
      if (ready4data){
        if ( !inited){
          //we are ready, connect the socket!
          ss = Util::Stream::getStream(streamName);
          if ( !ss.connected()){
  #if DEBUG >= 1
            fprintf(stderr, "Could not connect to server!\n");
  #endif
            Socket.close(); //disconnect user
            break;
          }
          ss.setBlocking(false);
          ss.SendNow("p\n");
          inited = true;
        }
        if (inited && !noStats){
          long long int now = Util::epoch();
          if (now != lastStats){
            lastStats = now;
            ss.SendNow(Socket.getStats("RTMP").c_str());
          }
        }
        if (ss.spool()){
          while (Strm.parsePacket(ss.Received())){
            if (playTransaction != -1){
              //send a status reply
              AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
              amfreply.addContent(AMF::Object("", "onStatus")); //status reply
              amfreply.addContent(AMF::Object("", (double)playTransaction)); //same transaction ID
              amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
              amfreply.addContent(AMF::Object("")); //info
              amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
              amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Reset"));
              amfreply.getContentP(3)->addContent(AMF::Object("description", "Playing and resetting..."));
              amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
              amfreply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
              sendCommand(amfreply, playMessageType, playStreamId);
              //send streamisrecorded if stream, well, is recorded.
              if (Strm.metadata.isMember("length") && Strm.metadata["length"].asInt() > 0){
                Socket.Send(RTMPStream::SendUSR(4, 1)); //send UCM StreamIsRecorded (4), stream 1
              }
              //send streambegin
              Socket.Send(RTMPStream::SendUSR(0, 1)); //send UCM StreamBegin (0), stream 1
              //and more reply
              amfreply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
              amfreply.addContent(AMF::Object("", "onStatus")); //status reply
              amfreply.addContent(AMF::Object("", (double)playTransaction)); //same transaction ID
              amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
              amfreply.addContent(AMF::Object("")); //info
              amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
              amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Start"));
              amfreply.getContentP(3)->addContent(AMF::Object("description", "Playing!"));
              amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
              amfreply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
              sendCommand(amfreply, playMessageType, playStreamId);
              RTMPStream::chunk_snd_max = 102400; //100KiB
              Socket.Send(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); //send chunk size max (msg 1)
              //send dunno?
              Socket.Send(RTMPStream::SendUSR(32, 1)); //send UCM no clue?, stream 1
              playTransaction = -1;
            }

            //sent init data if needed
            if ( !streamInited){
              init_tag.DTSCMetaInit(Strm);
              Socket.SendNow(RTMPStream::SendMedia(init_tag));
              if (Strm.metadata.isMember("audio") && Strm.metadata["audio"].isMember("init")){
                init_tag.DTSCAudioInit(Strm);
                Socket.SendNow(RTMPStream::SendMedia(init_tag));
              }
              if (Strm.metadata.isMember("video") && Strm.metadata["video"].isMember("init")){
                init_tag.DTSCVideoInit(Strm);
                Socket.SendNow(RTMPStream::SendMedia(init_tag));
              }
              streamInited = true;
            }
            //sent a tag
            tag.DTSCLoader(Strm);
            Socket.SendNow(RTMPStream::SendMedia(tag));
  #if DEBUG >= 8
            fprintf(stderr, "Sent tag to %i: [%u] %s\n", Socket.getSocket(), tag.tagTime(), tag.tagType().c_str());
  #endif
          }
        }
      }
    }
    Socket.close();
    ss.SendNow(Socket.getStats("RTMP").c_str());
    ss.close();
    return 0;
  } //Connector_RTMP
}

///\brief The standard process-spawning main function.
int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  conf.addConnectorOptions(1935);
  conf.parseArgs(argc, argv);
  Socket::Server server_socket = Socket::Server(conf.getInteger("listen_port"), conf.getString("listen_interface"));
  if ( !server_socket.connected()){
    return 1;
  }
  conf.activate();

  while (server_socket.connected() && conf.is_active){
    Socket::Connection S = server_socket.accept();
    if (S.connected()){ //check if the new connection is valid
      pid_t myid = fork();
      if (myid == 0){ //if new child, start MAINHANDLER
        return Connector_RTMP::rtmpConnector(S);
      }else{ //otherwise, do nothing or output debugging text
#if DEBUG >= 5
        fprintf(stderr, "Spawned new process %i for socket %i\n", (int)myid, S.getSocket());
#endif
      }
    }
  } //while connected
  server_socket.close();
  return 0;
} //main
