#include "chunkstream.cpp" //chunkstream decoding
#include "amf.cpp" //simple AMF0 parsing


//gets and parses one chunk
void parseChunk(){
  static chunkpack next;
  static AMFType amfdata("empty", (unsigned char)0xFF);
  static AMFType amfelem("empty", (unsigned char)0xFF);
  static int tmpint;
  next = getWholeChunk();
  switch (next.msg_type_id){
    case 0://does not exist
      break;//happens when connection breaks unexpectedly
    case 1://set chunk size
      chunk_rec_max = ntohl(*(int*)next.data);
      fprintf(stderr, "CTRL: Set chunk size: %i\n", chunk_rec_max);
      break;
    case 2://abort message - we ignore this one
      fprintf(stderr, "CTRL: Abort message\n");
      //4 bytes of stream id to drop
      break;
    case 3://ack
      fprintf(stderr, "CTRL: Acknowledgement\n");
      snd_window_at = ntohl(*(int*)next.data);
      //maybe better? snd_window_at = snd_cnt;
      break;
    case 4:{
      short int ucmtype = ntohs(*(short int*)next.data);
      fprintf(stderr, "CTRL: User control message %hi\n", ucmtype);
      //2 bytes event type, rest = event data
      //we don't need to process this
      } break;
    case 5://window size of other end
      fprintf(stderr, "CTRL: Window size\n");
      rec_window_size = ntohl(*(int*)next.data);
      break;
    case 6:
      fprintf(stderr, "CTRL: Set peer bandwidth\n");
      //4 bytes window size, 1 byte limit type (ignored)
      snd_window_size = ntohl(*(int*)next.data);
      SendCTL(5, snd_window_size);//send window acknowledgement size (msg 5)
      break;
    case 8:
      fprintf(stderr, "Received audio data\n");
      break;
    case 9:
      fprintf(stderr, "Received video data\n");
      break;
    case 15:
      fprintf(stderr, "Received AFM3 data message\n");
      break;
    case 16:
      fprintf(stderr, "Received AFM3 shared object\n");
      break;
    case 17:
      fprintf(stderr, "Received AFM3 command message\n");
      break;
    case 18:
      fprintf(stderr, "Received AFM0 data message\n");
      break;
    case 19:
      fprintf(stderr, "Received AFM0 shared object\n");
      break;
    case 20:{//AMF0 command message
      amfdata = parseAMF(next.data, next.real_len);
      fprintf(stderr, "Received AFM0 command message:\n");
      amfdata.Print();
      if (amfdata.getContentP(0)->StrValue() == "connect"){
        tmpint = amfdata.getContentP(2)->getContentP("videoCodecs")->NumValue();
        if (tmpint & 0x04){fprintf(stderr, "Sorensen video support detected\n");}
        if (tmpint & 0x80){fprintf(stderr, "H264 video support detected\n");}
        tmpint = amfdata.getContentP(2)->getContentP("audioCodecs")->NumValue();
        if (tmpint & 0x04){fprintf(stderr, "MP3 audio support detected\n");}
        if (tmpint & 0x400){fprintf(stderr, "AAC video support detected\n");}
        //send a _result reply
        AMFType amfreply("container", (unsigned char)0xFF);
        amfreply.addContent(AMFType("", "_result"));//result success
        amfreply.addContent(amfdata.getContent(1));//same transaction ID
        amfreply.addContent(AMFType("", (double)0, 0x05));//null - properties (none?)
        amfreply.addContent(AMFType(""));//info
        amfreply.getContentP(3)->addContent(AMFType("level", "status"));
        amfreply.getContentP(3)->addContent(AMFType("code", "NetConnection.Connect.Sucess"));
        amfreply.getContentP(3)->addContent(AMFType("description", "Connection succeeded."));
        SendChunk(3, 20, next.msg_stream_id, amfreply.Pack());
        SendCTL(5, snd_window_size);//send window acknowledgement size (msg 5)
        SendCTL(5, rec_window_size, 1);//send peer bandwidth (msg 6)
        SendUSR(0, 10);//send UCM StreamBegin (0), stream 10 (we use this number)
      }//connect
      if (amfdata.getContentP(0)->StrValue() == "createStream"){
        //send a _result reply
        AMFType amfreply("container", (unsigned char)0xFF);
        amfreply.addContent(AMFType("", "_result"));//result success
        amfreply.addContent(amfdata.getContent(1));//same transaction ID
        amfreply.addContent(AMFType("", (double)0, 0x05));//null - command info
        amfreply.addContent(AMFType("", (double)10));//stream ID - we use 10
        SendChunk(3, 20, next.msg_stream_id, amfreply.Pack());
      }//createStream
      if ((amfdata.getContentP(0)->StrValue() == "play") || (amfdata.getContentP(0)->StrValue() == "play2")){
        //send a status reply
        AMFType amfreply("container", (unsigned char)0xFF);
        amfreply.addContent(AMFType("", "onStatus"));//status reply
        amfreply.addContent(AMFType("", "NetStream.Play.Start"));//result success
        SendChunk(3, 20, next.msg_stream_id, amfreply.Pack());
        ready4data = true;//start sending video data!
      }//createStream
    } break;
    case 22:
      fprintf(stderr, "Received aggregate message\n");
      break;
    default:
      fprintf(stderr, "Unknown chunk received!\n");
      break;
  }
}//parseChunk
