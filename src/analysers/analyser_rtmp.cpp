#include <string>
#include "analyser_rtmp.h"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <mist/flv_tag.h>
#include <mist/amf.h>
#include <mist/rtmpchunks.h>
#include <mist/config.h>
#include <mist/socket.h>
#include <sys/sysinfo.h>
#include <signal.h>

#define DETAIL_RECONSTRUCT 1
#define DETAIL_EXPLICIT 2
#define DETAIL_VERBOSE 4

rtmpAnalyser::rtmpAnalyser(Util::Config config) : analysers(config)
{  
  std::cout << "rtmp constr" << std::endl; 
  Detail = conf.getInteger("detail");
  //Detail = 4;

  inbuffer.reserve(3073);

  while(std::cin.good() && inbuffer.size() < 3073){
    inbuffer += std::cin.get();

  }
  inbuffer.erase(0,3073); //strip the handshake part

  AMF::Object amfdata("empty", AMF::AMF0_DDV_CONTAINER);
  AMF::Object3 amf3data("empty", AMF::AMF3_DDV_CONTAINER);

  RTMPStream::rec_cnt += 3073;
  
  read_in = 0;
  endTime = 0;
}

bool rtmpAnalyser::packetReady()
{
  return (std::cin.good() || strbuf.size()); 
}

int rtmpAnalyser::doAnalyse()
{  
//  std::cout << "do analyse" << std::endl;

  analyse=true;
  if (analyse){ //always analyse..?
//    std::cout << "status strbuf: " << next.Parse(strbuf) << " strbuf_size: " << strbuf.size() << std::endl;

      if (next.Parse(strbuf)){
        if (Detail & DETAIL_VERBOSE){
          fprintf(stderr, "Chunk info: [%#2X] CS ID %u, timestamp %u, len %u, type ID %u, Stream ID %u\n", next.headertype, next.cs_id, next.timestamp,
              next.len, next.msg_type_id, next.msg_stream_id);
        }
        switch (next.msg_type_id){
          case 0: //does not exist
            fprintf(stderr, "Error chunk @ %lu - CS%i, T%i, L%i, LL%i, MID%i\n", read_in-inbuffer.size(), next.cs_id, next.timestamp, next.real_len, next.len_left, next.msg_stream_id);
            return 0;
            break; //happens when connection breaks unexpectedly
          case 1: //set chunk size
            RTMPStream::chunk_rec_max = ntohl(*(int*)next.data.c_str());
            fprintf(stderr, "CTRL: Set chunk size: %i\n", RTMPStream::chunk_rec_max);
            break;
          case 2: //abort message - we ignore this one
            fprintf(stderr, "CTRL: Abort message: %i\n", ntohl(*(int*)next.data.c_str()));
            //4 bytes of stream id to drop
            break;
          case 3: //ack
            RTMPStream::snd_window_at = ntohl(*(int*)next.data.c_str());
            fprintf(stderr, "CTRL: Acknowledgement: %i\n", RTMPStream::snd_window_at);
            break;
          case 4: {
            short int ucmtype = ntohs(*(short int*)next.data.c_str());
            switch (ucmtype){
              case 0:
                fprintf(stderr, "CTRL: User control message: stream begin %u\n", ntohl(*(unsigned int*)(next.data.c_str()+2)));
                break;
              case 1:
                fprintf(stderr, "CTRL: User control message: stream EOF %u\n", ntohl(*(unsigned int*)(next.data.c_str()+2)));
                break;
              case 2:
                fprintf(stderr, "CTRL: User control message: stream dry %u\n", ntohl(*(unsigned int*)(next.data.c_str()+2)));
                break;
              case 3:
                fprintf(stderr, "CTRL: User control message: setbufferlen %u\n", ntohl(*(unsigned int*)(next.data.c_str()+2)));
                break;
              case 4:
                fprintf(stderr, "CTRL: User control message: streamisrecorded %u\n", ntohl(*(unsigned int*)(next.data.c_str()+2)));
                break;
              case 6:
                fprintf(stderr, "CTRL: User control message: pingrequest %u\n", ntohl(*(unsigned int*)(next.data.c_str()+2)));
                break;
              case 7:
                fprintf(stderr, "CTRL: User control message: pingresponse %u\n", ntohl(*(unsigned int*)(next.data.c_str()+2)));
                break;
              case 31:
              case 32:
                //don't know, but not interesting anyway
                break;
              default:
                fprintf(stderr, "CTRL: User control message: UNKNOWN %hu - %u\n", ucmtype, ntohl(*(unsigned int*)(next.data.c_str()+2)));
                break;
            }
          }
            break;
          case 5: //window size of other end
            RTMPStream::rec_window_size = ntohl(*(int*)next.data.c_str());
            RTMPStream::rec_window_at = RTMPStream::rec_cnt;
            fprintf(stderr, "CTRL: Window size: %i\n", RTMPStream::rec_window_size);
            break;
          case 6:
            RTMPStream::snd_window_size = ntohl(*(int*)next.data.c_str());
            //4 bytes window size, 1 byte limit type (ignored)
            fprintf(stderr, "CTRL: Set peer bandwidth: %i\n", RTMPStream::snd_window_size);
            break;
          case 8:
          case 9:
            if (Detail & (DETAIL_EXPLICIT | DETAIL_RECONSTRUCT)){
              F.ChunkLoader(next);
              if (Detail & DETAIL_EXPLICIT){
                std::cerr << "[" << F.tagTime() << "+" << F.offset() << "] " << F.tagType() << std::endl;
              }
              if (Detail & DETAIL_RECONSTRUCT){
                std::cout.write(F.data, F.len);
              }
            }
            break;
          case 15:
            fprintf(stderr, "Received AFM3 data message\n");
            break;
          case 16:
            fprintf(stderr, "Received AFM3 shared object\n");
            break;
          case 17: {
            fprintf(stderr, "Received AFM3 command message:\n");
            char soort = next.data[0];
            next.data = next.data.substr(1);
            if (soort == 0){
              amfdata = AMF::parse(next.data);
              std::cerr << amfdata.Print() << std::endl;
            }else{
              amf3data = AMF::parse3(next.data);
              amf3data.Print();
            }
          }
            break;
          case 18: {
            fprintf(stderr, "Received AFM0 data message (metadata):\n");
            amfdata = AMF::parse(next.data);
            amfdata.Print();
            if (Detail & DETAIL_RECONSTRUCT){
              F.ChunkLoader(next);
              std::cout.write(F.data, F.len);
            }
          }
            break;
          case 19:
            fprintf(stderr, "Received AFM0 shared object\n");
            break;
          case 20: { //AMF0 command message
            fprintf(stderr, "Received AFM0 command message:\n");
            amfdata = AMF::parse(next.data);
            std::cerr << amfdata.Print() << std::endl;
          }
            break;
          case 22:
            if (Detail & DETAIL_RECONSTRUCT){
              std::cout << next.data;
            }
            break;
          default:
            fprintf(stderr, "Unknown chunk received! Probably protocol corruption, stopping parsing of incoming data.\n");
            return 1;
            break;
        } //switch for type of chunk
      }else{ //if chunk parsed
        if (std::cin.good()){
          unsigned int charCount = 0;
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
        }else{
          strbuf.get().clear();
        }
      }
  }

  return endTime;
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0]);
  
  analysers::defaultConfig(conf);
  conf.parseArgs(argc, argv);
  rtmpAnalyser A(conf);
  
  A.Run();
	
  return 0;
}
