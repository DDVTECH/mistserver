#include "chunkstream.cpp" //chunkstream decoding
#include "amf.cpp" //simple AMF0 parsing
std::string streamname = "/tmp/shared_socket";

//gets and parses one chunk
void parseChunk(){
  static chunkpack next;
  static AMFType amfdata("empty", (unsigned char)0xFF);
  static AMFType amfelem("empty", (unsigned char)0xFF);
  next = getWholeChunk();
  switch (next.msg_type_id){
    case 0://does not exist
      break;//happens when connection breaks unexpectedly
    case 1://set chunk size
      chunk_rec_max = ntohl(*(int*)next.data);
      #if DEBUG >= 4
      fprintf(stderr, "CTRL: Set chunk size: %i\n", chunk_rec_max);
      #endif
      break;
    case 2://abort message - we ignore this one
      #if DEBUG >= 4
      fprintf(stderr, "CTRL: Abort message\n");
      #endif
      //4 bytes of stream id to drop
      break;
    case 3://ack
      #if DEBUG >= 4
      fprintf(stderr, "CTRL: Acknowledgement\n");
      #endif
      snd_window_at = ntohl(*(int*)next.data);
      snd_window_at = snd_cnt;
      break;
    case 4:{
      #if DEBUG >= 4
      short int ucmtype = ntohs(*(short int*)next.data);
      fprintf(stderr, "CTRL: User control message %hi\n", ucmtype);
      #endif
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
      SendCTL(3, rec_cnt);//send ack (msg 3)
    } break;
    case 5://window size of other end
      #if DEBUG >= 4
      fprintf(stderr, "CTRL: Window size\n");
      #endif
      rec_window_size = ntohl(*(int*)next.data);
      rec_window_at = rec_cnt;
      SendCTL(3, rec_cnt);//send ack (msg 3)
      break;
    case 6:
      #if DEBUG >= 4
      fprintf(stderr, "CTRL: Set peer bandwidth\n");
      #endif
      //4 bytes window size, 1 byte limit type (ignored)
      snd_window_size = ntohl(*(int*)next.data);
      SendCTL(5, snd_window_size);//send window acknowledgement size (msg 5)
      break;
    case 8:
      #if DEBUG >= 4
      fprintf(stderr, "Received audio data\n");
      #endif
      break;
    case 9:
      #if DEBUG >= 4
      fprintf(stderr, "Received video data\n");
      #endif
      break;
    case 15:
      #if DEBUG >= 4
      fprintf(stderr, "Received AFM3 data message\n");
      #endif
      break;
    case 16:
      #if DEBUG >= 4
      fprintf(stderr, "Received AFM3 shared object\n");
      #endif
      break;
    case 17:
      #if DEBUG >= 4
      fprintf(stderr, "Received AFM3 command message\n");
      #endif
      break;
    case 18:
      #if DEBUG >= 4
      fprintf(stderr, "Received AFM0 data message\n");
      #endif
      break;
    case 19:
      #if DEBUG >= 4
      fprintf(stderr, "Received AFM0 shared object\n");
      #endif
      break;
    case 20:{//AMF0 command message
      bool parsed = false;
      amfdata = parseAMF(next.data, next.real_len);
      #if DEBUG >= 4
      amfdata.Print();
      #endif
      if (amfdata.getContentP(0)->StrValue() == "connect"){
        #if DEBUG >= 4
        int tmpint;
        tmpint = amfdata.getContentP(2)->getContentP("videoCodecs")->NumValue();
        if (tmpint & 0x04){fprintf(stderr, "Sorensen video support detected\n");}
        if (tmpint & 0x80){fprintf(stderr, "H264 video support detected\n");}
        tmpint = amfdata.getContentP(2)->getContentP("audioCodecs")->NumValue();
        if (tmpint & 0x04){fprintf(stderr, "MP3 audio support detected\n");}
        if (tmpint & 0x400){fprintf(stderr, "AAC video support detected\n");}
        #endif
        SendCTL(6, rec_window_size, 0);//send peer bandwidth (msg 6)
        SendCTL(5, snd_window_size);//send window acknowledgement size (msg 5)
        SendUSR(0, 1);//send UCM StreamBegin (0), stream 1
        //send a _result reply
        AMFType amfreply("container", (unsigned char)0xFF);
        amfreply.addContent(AMFType("", "_result"));//result success
        amfreply.addContent(amfdata.getContent(1));//same transaction ID
//        amfreply.addContent(AMFType("", (double)0, 0x05));//null - command info
        amfreply.addContent(AMFType(""));//server properties
        amfreply.getContentP(2)->addContent(AMFType("fmsVer", "FMS/3,0,1,123"));//stolen from examples
        amfreply.getContentP(2)->addContent(AMFType("capabilities", (double)31));//stolen from examples
        amfreply.addContent(AMFType(""));//info
        amfreply.getContentP(3)->addContent(AMFType("level", "status"));
        amfreply.getContentP(3)->addContent(AMFType("code", "NetConnection.Connect.Success"));
        amfreply.getContentP(3)->addContent(AMFType("description", "Connection succeeded."));
        amfreply.getContentP(3)->addContent(AMFType("capabilities", (double)33));//from red5 server
        amfreply.getContentP(3)->addContent(AMFType("fmsVer", "PLS/1,0,0,0"));//from red5 server
        #if DEBUG >= 4
        amfreply.Print();
        #endif
        SendChunk(3, 20, next.msg_stream_id, amfreply.Pack());
        //send onBWDone packet
        //amfreply = AMFType("container", (unsigned char)0xFF);
        //amfreply.addContent(AMFType("", "onBWDone"));//result success
        //amfreply.addContent(AMFType("", (double)0));//zero
        //amfreply.addContent(AMFType("", (double)0, 0x05));//null
        //SendChunk(3, 20, next.msg_stream_id, amfreply.Pack());
        parsed = true;
      }//connect
      if (amfdata.getContentP(0)->StrValue() == "createStream"){
        //send a _result reply
        AMFType amfreply("container", (unsigned char)0xFF);
        amfreply.addContent(AMFType("", "_result"));//result success
        amfreply.addContent(amfdata.getContent(1));//same transaction ID
        amfreply.addContent(AMFType("", (double)0, 0x05));//null - command info
        amfreply.addContent(AMFType("", (double)1));//stream ID - we use 1
        #if DEBUG >= 4
        amfreply.Print();
        #endif
        SendChunk(3, 20, next.msg_stream_id, amfreply.Pack());
        SendUSR(0, 1);//send UCM StreamBegin (0), stream 1
        parsed = true;
      }//createStream
      if ((amfdata.getContentP(0)->StrValue() == "getStreamLength") || (amfdata.getContentP(0)->StrValue() == "getMovLen")){
        //send a _result reply
        AMFType amfreply("container", (unsigned char)0xFF);
        amfreply.addContent(AMFType("", "_result"));//result success
        amfreply.addContent(amfdata.getContent(1));//same transaction ID
        amfreply.addContent(AMFType("", (double)0, 0x05));//null - command info
        amfreply.addContent(AMFType("", (double)0));//zero length
        #if DEBUG >= 4
        amfreply.Print();
        #endif
        SendChunk(3, 20, next.msg_stream_id, amfreply.Pack());
        parsed = true;
      }//getStreamLength
      if (amfdata.getContentP(0)->StrValue() == "checkBandwidth"){
        //send a _result reply
        AMFType amfreply("container", (unsigned char)0xFF);
        amfreply.addContent(AMFType("", "_result"));//result success
        amfreply.addContent(amfdata.getContent(1));//same transaction ID
        amfreply.addContent(AMFType("", (double)0, 0x05));//null - command info
        amfreply.addContent(AMFType("", (double)0, 0x05));//null - command info
        #if DEBUG >= 4
        amfreply.Print();
        #endif
        SendChunk(3, 20, 1, amfreply.Pack());
        parsed = true;
      }//checkBandwidth
      if ((amfdata.getContentP(0)->StrValue() == "play") || (amfdata.getContentP(0)->StrValue() == "play2")){
        //send streambegin
        streamname = amfdata.getContentP(3)->StrValue();
        for (std::string::iterator i=streamname.end()-1; i>=streamname.begin(); --i){
          if (!isalpha(*i) && !isdigit(*i)){streamname.erase(i);}else{*i=tolower(*i);}
        }
        streamname = "/tmp/shared_socket_" + streamname;
        SendUSR(0, 1);//send UCM StreamBegin (0), stream 1
        //send a status reply
        AMFType amfreply("container", (unsigned char)0xFF);
        amfreply.addContent(AMFType("", "onStatus"));//status reply
        amfreply.addContent(amfdata.getContent(1));//same transaction ID
        amfreply.addContent(AMFType("", (double)0, 0x05));//null - command info
        amfreply.addContent(AMFType(""));//info
        amfreply.getContentP(3)->addContent(AMFType("level", "status"));
        amfreply.getContentP(3)->addContent(AMFType("code", "NetStream.Play.Reset"));
        amfreply.getContentP(3)->addContent(AMFType("description", "Playing and resetting..."));
        amfreply.getContentP(3)->addContent(AMFType("details", "PLS"));
        amfreply.getContentP(3)->addContent(AMFType("clientid", (double)1));
        #if DEBUG >= 4
        amfreply.Print();
        #endif
        SendChunk(4, 20, next.msg_stream_id, amfreply.Pack());
        amfreply = AMFType("container", (unsigned char)0xFF);
        amfreply.addContent(AMFType("", "onStatus"));//status reply
        amfreply.addContent(amfdata.getContent(1));//same transaction ID
        amfreply.addContent(AMFType("", (double)0, 0x05));//null - command info
        amfreply.addContent(AMFType(""));//info
        amfreply.getContentP(3)->addContent(AMFType("level", "status"));
        amfreply.getContentP(3)->addContent(AMFType("code", "NetStream.Play.Start"));
        amfreply.getContentP(3)->addContent(AMFType("description", "Playing!"));
        amfreply.getContentP(3)->addContent(AMFType("details", "PLS"));
        amfreply.getContentP(3)->addContent(AMFType("clientid", (double)1));
        #if DEBUG >= 4
        amfreply.Print();
        #endif
        SendChunk(4, 20, 1, amfreply.Pack());
//No clue what this does. Most real servers send it, though...
//        amfreply = AMFType("container", (unsigned char)0xFF);
//        amfreply.addContent(AMFType("", "|RtmpSampleAccess"));//status reply
//        amfreply.addContent(AMFType("", (double)1, 0x01));//bool true - audioaccess
//        amfreply.addContent(AMFType("", (double)1, 0x01));//bool true - videoaccess
//        SendChunk(4, 20, next.msg_stream_id, amfreply.Pack());
        chunk_snd_max = 1024*1024;
        SendCTL(1, chunk_snd_max);//send chunk size max (msg 1)
        ready4data = true;//start sending video data!
        parsed = true;
      }//createStream
      #if DEBUG >= 3
      fprintf(stderr, "AMF0 command: %s\n", amfdata.getContentP(0)->StrValue().c_str());
      #endif
      if (!parsed){
        #if DEBUG >= 2
        fprintf(stderr, "AMF0 command not processed! :(\n");
        #endif
      }
    } break;
    case 22:
      #if DEBUG >= 4
      fprintf(stderr, "Received aggregate message\n");
      #endif
      break;
    default:
      #if DEBUG >= 1
      fprintf(stderr, "Unknown chunk received! Probably protocol corruption, stopping parsing of incoming data.\n");
      #endif
      stopparsing = true;
      break;
  }
}//parseChunk
