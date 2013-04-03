/// \file rtmp_analyser.cpp
/// Debugging tool for RTMP data.
#include <iostream>
#include <fstream>
#include <string>

#include <cstdlib>

#include <mist/flv_tag.h>
#include <mist/amf.h>
#include <mist/rtmpchunks.h>
#include <mist/config.h>

#define DETAIL_RECONSTRUCT 1
#define DETAIL_EXPLICIT 2
#define DETAIL_VERBOSE 4

///\brief Holds everything unique to the analysers.
namespace Analysers {
  ///\brief Debugging tool for RTMP data.
  ///
  ///Expects RTMP data of one side of the conversation through stdin, outputs human-readable information to stderr.
  ///
  ///Will output FLV file to stdout, if available.
  ///
  ///Automatically skips the handshake data.
  ///\param conf The configuration parsed from the commandline.
  ///\return The return code of the analyser.
  int analyseRTMP(Util::Config conf){
    int Detail = conf.getInteger("detail");
    if (Detail > 0){
      fprintf(stderr, "Detail level set:\n");
      if (Detail & DETAIL_RECONSTRUCT){
        fprintf(stderr, " - Will reconstuct FLV file to stdout\n");
        std::cout.write(FLV::Header, 13);
      }
      if (Detail & DETAIL_EXPLICIT){
        fprintf(stderr, " - Will list explicit video/audio data information\n");
      }
      if (Detail & DETAIL_VERBOSE){
        fprintf(stderr, " - Will list verbose chunk information\n");
      }
    }

    std::string inbuffer;
    inbuffer.reserve(3073);
    while (std::cin.good() && inbuffer.size() < 3073){
      inbuffer += std::cin.get();
    } //read all of std::cin to temp
    inbuffer.erase(0, 3073); //strip the handshake part
    RTMPStream::Chunk next;
    FLV::Tag F; //FLV holder
    AMF::Object amfdata("empty", AMF::AMF0_DDV_CONTAINER);
    AMF::Object3 amf3data("empty", AMF::AMF3_DDV_CONTAINER);

    while (std::cin.good() || inbuffer.size()){
      if (next.Parse(inbuffer)){
        if (Detail & DETAIL_VERBOSE){
          fprintf(stderr, "Chunk info: [%#2X] CS ID %u, timestamp %u, len %u, type ID %u, Stream ID %u\n", next.headertype, next.cs_id, next.timestamp,
              next.len, next.msg_type_id, next.msg_stream_id);
        }
        switch (next.msg_type_id){
          case 0: //does not exist
            fprintf(stderr, "Error chunk - %i, %i, %i, %i, %i\n", next.cs_id, next.timestamp, next.real_len, next.len_left, next.msg_stream_id);
            //return 0;
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
            if (Detail & (DETAIL_EXPLICIT | DETAIL_RECONSTRUCT)){
              F.ChunkLoader(next);
              if (Detail & DETAIL_EXPLICIT){
                fprintf(stderr, "Received %i bytes audio data\n", next.len);
                std::cerr << "Got a " << F.len << " bytes " << F.tagType() << " FLV tag of time " << F.tagTime() << "." << std::endl;
              }
              if (Detail & DETAIL_RECONSTRUCT){
                std::cout.write(F.data, F.len);
              }
            }
            break;
          case 9:
            if (Detail & (DETAIL_EXPLICIT | DETAIL_RECONSTRUCT)){
              F.ChunkLoader(next);
              if (Detail & DETAIL_EXPLICIT){
                fprintf(stderr, "Received %i bytes video data\n", next.len);
                std::cerr << "Got a " << F.len << " bytes " << F.tagType() << " FLV tag of time " << F.tagTime() << "." << std::endl;
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
            fprintf(stderr, "Received aggregate message\n");
            break;
          default:
            fprintf(stderr, "Unknown chunk received! Probably protocol corruption, stopping parsing of incoming data.\n");
            return 1;
            break;
        } //switch for type of chunk
      }else{ //if chunk parsed
        if (std::cin.good()){
          inbuffer += std::cin.get();
        }else{
          inbuffer.clear();
        }
      }
    }//while std::cin.good()
    fprintf(stderr, "No more readable data\n");
    return 0;
  }
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("detail",
      JSON::fromString(
          "{\"arg_num\":1, \"arg\":\"integer\", \"default\":0, \"help\":\"Bitmask, 1 = Reconstruct, 2 = Explicit media info, 4 = Verbose chunks\"}"));
  conf.parseArgs(argc, argv);

  return Analysers::analyseRTMP(conf);
} //main
