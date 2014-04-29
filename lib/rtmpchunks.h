/// \file rtmpchunks.h
/// Holds all headers for the RTMPStream namespace.

#pragma once
#include <map>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string>
#include <arpa/inet.h>
#include "socket.h"

//forward declaration of FLV::Tag to avoid circular dependencies.
namespace FLV {
  class Tag;
}

/// Contains all functions and classes needed for RTMP connections.
namespace RTMPStream {

  extern unsigned int chunk_rec_max; ///< Maximum size for a received chunk.
  extern unsigned int chunk_snd_max; ///< Maximum size for a sent chunk.
  extern unsigned int rec_window_size; ///< Window size for receiving.
  extern unsigned int snd_window_size; ///< Window size for sending.
  extern unsigned int rec_window_at; ///< Current position of the receiving window.
  extern unsigned int snd_window_at; ///< Current position of the sending window.
  extern unsigned int rec_cnt; ///< Counter for total data received, in bytes.
  extern unsigned int snd_cnt; ///< Counter for total data sent, in bytes.

  extern timeval lastrec; ///< Timestamp of last time data was received.
  
  /// Holds a single RTMP chunk, either send or receive direction.
  class Chunk{
    public:
      unsigned char headertype; ///< For input chunks, the type of header. This is calculated automatically for output chunks.
      unsigned int cs_id; ///< ContentStream ID
      unsigned int timestamp; ///< Timestamp of this chunk.
      unsigned int len; ///< Length of the complete chunk.
      unsigned int real_len; ///< Length of this particular part of it.
      unsigned int len_left; ///< Length not yet received, out of complete chunk.
      unsigned char msg_type_id; ///< Message Type ID
      unsigned int msg_stream_id; ///< Message Stream ID
      std::string data; ///< Payload of chunk.

      Chunk();
      bool Parse(std::string & data);
      bool Parse(Socket::Buffer & data);
      std::string & Pack();
  };
  //RTMPStream::Chunk

  extern std::map<unsigned int, Chunk> lastsend;
  extern std::map<unsigned int, Chunk> lastrecv;
  
  std::string & SendChunk(unsigned int cs_id, unsigned char msg_type_id, unsigned int msg_stream_id, std::string data);
  std::string & SendMedia(unsigned char msg_type_id, unsigned char * data, int len, unsigned int ts);
  std::string & SendMedia(FLV::Tag & tag);
  std::string & SendCTL(unsigned char type, unsigned int data);
  std::string & SendCTL(unsigned char type, unsigned int data, unsigned char data2);
  std::string & SendUSR(unsigned char type, unsigned int data);
  std::string & SendUSR(unsigned char type, unsigned int data, unsigned int data2);

  /// This value should be set to the first 1537 bytes received.
  extern std::string handshake_in;
  /// This value is the handshake response that is to be sent out.
  extern std::string handshake_out;
  /// Does the handshake. Expects handshake_in to be filled, and fills handshake_out.
  bool doHandshake();
} //RTMPStream namespace
