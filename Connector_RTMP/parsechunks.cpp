#include "chunkstream.cpp" //chunkstream decoding
#include "amf.cpp" //simple AMF0 parsing


//gets and parses one chunk
void parseChunk(){
  static chunkpack next;
  static std::vector<AMFType> * amfdata = 0;
  static AMFType * amfelem = 0;
  static int tmpint;
  next = getWholeChunk();
  if (next.cs_id == 2 && next.msg_stream_id == 0){
    fprintf(stderr, "Received protocol message. (cs_id 2, stream id 0)\nContents:\n");
    fwrite(next.data, 1, next.real_len, stderr);
    fflush(stderr);
  }
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
    case 4:
      fprintf(stderr, "CTRL: User control message\n");
      //2 bytes event type, rest = event data
      //TODO: process this
      break;
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
    case 20:
      if (amfdata != 0){delete amfdata;}
      amfdata = parseAMF(next.data, next.real_len);
      fprintf(stderr, "Received AFM0 command message: %s\n", (*amfdata)[0].StrValue().c_str());
      if ((*amfdata)[0].StrValue() == "connect"){
        tmpint = getAMF(amfdata, "videoCodecs")->NumValue();
        if (tmpint & 0x04){fprintf(stderr, "Sorensen video support detected\n");}
        if (tmpint & 0x80){fprintf(stderr, "H264 video support detected\n");}
        tmpint = getAMF(amfdata, "audioCodecs")->NumValue();
        if (tmpint & 0x04){fprintf(stderr, "MP3 audio support detected\n");}
        if (tmpint & 0x400){fprintf(stderr, "AAC video support detected\n");}
        SendCTL(5, snd_window_size);//send window acknowledgement size (msg 5)
        SendCTL(5, rec_window_size, 1);//send peer bandwidth (msg 6)
        SendUSR(0, 10);//send UCM StreamBegin (0), stream 10 (we use this number)
        //send AFM0 (20)  {_result, 1, {properties}, {info}}
      }else{
        //call, close, createStream
        //TODO: play (&& play2?)
        fprintf(stderr, "Ignored AFM0 command.\n");
      }
      break;
    case 22:
      fprintf(stderr, "Received aggregate message\n");
      break;
    default:
      fprintf(stderr, "Unknown chunk received!\n");
      break;
  }
}//parseChunk
