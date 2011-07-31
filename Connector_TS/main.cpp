#include <queue>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <getopt.h>
#include <iostream>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include "../util/socket.h"
#include "../util/flv_tag.h"

class Transport_Packet {
  public:
    Transport_Packet( bool NALUStart = false, int PID = 0x100 );
    void SetPayload( char * Payload, int PayloadLen, int Offset = 13 );
    void SetPesHeader( );
    void Write( );
    void Write( Socket::Connection conn );
    void SetContinuityCounter( int Counter );
    void SetMessageLength( int MsgLen );
  private:
    int PID;
    char Buffer[188];
};//Transport Packet

Transport_Packet::Transport_Packet( bool NALUStart, int PID ) {
  (*this).PID = PID;
  Buffer[0] = (char)0x47;
  Buffer[1] = ( NALUStart ? (char)0x40 : 0 ) + (( PID & 0xFF00 ) >> 8 );
  Buffer[2] = ( PID & 0x00FF );
  Buffer[3] = (char)0x00;
}

void Transport_Packet::SetMessageLength( int MsgLen ) {
  Buffer[8] = ( MsgLen & 0xFF00 ) >> 8;
  Buffer[9] = ( MsgLen & 0xFF );
}

void Transport_Packet::SetContinuityCounter( int Counter ) {
  Buffer[3] = (char)0x00 + ( Counter & 0x0F );
}

void Transport_Packet::SetPesHeader( ) {
  Buffer[4] = (char)0x00;
  Buffer[5] = (char)0x00;
  Buffer[6] = (char)0x01;
  Buffer[7] = (char)0xE0;
  Buffer[8] = (char)0xFF;
  Buffer[9] = (char)0xFF;
  Buffer[10] = (char)0x80;
  Buffer[11] = (char)0x00;
  Buffer[12] = (char)0x00;
}

void Transport_Packet::SetPayload( char * Payload, int PayloadLen, int Offset ) {
//  std::cerr << "\tSetPayload::Writing " << std::min( PayloadLen, 188-Offset ) << " bytes\n";
  memcpy( &Buffer[Offset], Payload, std::min( PayloadLen, 188-Offset ) );
}

std::vector<Transport_Packet> WrapNalus( FLV::Tag tag ) {
  static int ContinuityCounter = 0;
  Transport_Packet TS;
  int PacketAmount = ( ( tag.len - (188 - 17 ) ) / 184 ) + 2;
  std::cerr << "Wrapping a tag of length " << tag.len << " into " << PacketAmount << " TS Packet(s)\n";
  std::vector<Transport_Packet> Result;
  char LeadIn[4] = { (char)0x00, (char)0x00, (char)0x00, (char)0x01 };
  TS = Transport_Packet( true );
  TS.SetContinuityCounter( ContinuityCounter );
  ContinuityCounter = ( ( ContinuityCounter + 1 ) & 0x0F );
  TS.SetPesHeader( );
  TS.SetMessageLength( tag.len - 16 );
  TS.SetPayload( LeadIn, 4, 13 );
  TS.SetPayload( &tag.data[16], 171, 17 );
  Result.push_back( TS );
  for( int i = 0; i < (PacketAmount - 1); i++ ) {
    TS = Transport_Packet( false );
    TS.SetContinuityCounter( ContinuityCounter );
    ContinuityCounter = ( ( ContinuityCounter + 1 ) & 0x0F );
    TS.SetPayload( &tag.data[187+(184*i)], 184, 4 );
    Result.push_back( TS );
  }
  return Result;
}

void Transport_Packet::Write( ) {
  for( int i = 0; i < 188; i++ ) { std::cout << Buffer[i]; }
}

void Transport_Packet::Write( Socket::Connection conn ) {
  conn.write( Buffer, 188 );
}

int TS_Handler( Socket::Connection conn ) {
  FLV::Tag tag;///< Temporary tag buffer for incoming video data.
  bool inited = false;
  bool firstvideo = true;
  Socket::Connection ss(-1);
  while(conn.connected() && !FLV::Parse_Error) {
    if( !inited ) {
      ss = Socket::Connection("/tmp/shared_socket_fifa");
      if (!ss.connected()){
        #if DEBUG >= 1
        fprintf(stderr, "Could not connect to server!\n");
        #endif
        conn.close();
        break;
      }
      #if DEBUG >= 3
      fprintf(stderr, "Everything connected, starting to send video data...\n");
      #endif
      inited = true;
    }
    
    switch (ss.ready()){
      case -1:
        conn.close();
        #if DEBUG >= 1
        fprintf(stderr, "Source socket is disconnected.\n");
        #endif
        break;
      case 0: break;//not ready yet
      default:
        if (tag.SockLoader(ss)){//able to read a full packet?
          if( tag.data[ 0 ] == 0x09 ) {
            if( ( ( tag.data[ 11 ] & 0x0F ) == 7 ) && ( tag.data[ 12 ] == 1 ) ) {
              fprintf(stderr, "Video contains NALU\n" );
              if( firstvideo ) {
                firstvideo = false;
              } else {
                std::vector<Transport_Packet> Meh = WrapNalus( tag );
                std::cerr << "Received " << Meh.size( ) << " Transport Packet(s)\n";
                for( int i = 0; i < Meh.size( ); i++ ) {
                  Meh[i].Write( conn );
                }
              }
            }
          }
          if( tag.data[ 0 ] == 0x08 ) {
            if( ( tag.data[ 11 ] == 0xAF ) && ( tag.data[ 12 ] == 0x01 ) ) {
              fprintf(stderr, "Audio Contains Raw AAC\n");
            }
          }          
        }
        break;
    }
  }
  return 0;
}

#define DEFAULT_PORT 8888
#define MAINHANDLER TS_Handler
#define CONFIGSECT TS
#include "../util/server_setup.cpp"
