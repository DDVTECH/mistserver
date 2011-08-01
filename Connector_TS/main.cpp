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
    Transport_Packet( bool PacketStart = false, int PID = 0x100 );
    void SetPayload( char * Payload, int PayloadLen, int Offset = 13 );
    void SetPesHeader( int Offset = 4 );
    void Write( );
    void Write( Socket::Connection conn );
    void SetContinuityCounter( int Counter );
    void SetMessageLength( int MsgLen );
    void SetAdaptationField( );
    void CreatePAT( int ContinuityCounter );
    void CreatePMT( int ContinuityCounter );
  private:
    int PID;
    char Buffer[188];
};//Transport Packet

Transport_Packet::Transport_Packet( bool PacketStart, int PID ) {
  (*this).PID = PID;
  Buffer[0] = (char)0x47;
  Buffer[1] = ( PacketStart ? (char)0x40 : 0 ) + (( PID & 0xFF00 ) >> 8 );
  Buffer[2] = ( PID & 0x00FF );
  Buffer[3] = (char)0x00;
}

void Transport_Packet::SetMessageLength( int MsgLen ) {
  Buffer[8] = ( MsgLen & 0xFF00 ) >> 8;
  Buffer[9] = ( MsgLen & 0xFF );
}

void Transport_Packet::SetContinuityCounter( int Counter ) {
  Buffer[3] = ( Buffer[3] & 0xF0 ) + ( Counter & 0x0F );
}

void Transport_Packet::SetPesHeader( int Offset ) {
  Buffer[Offset] = (char)0x00;
  Buffer[Offset+1] = (char)0x00;
  Buffer[Offset+2] = (char)0x01;
  Buffer[Offset+3] = (char)0xE0;
  Buffer[Offset+4] = (char)0xFF;
  Buffer[Offset+5] = (char)0xFF;
  Buffer[Offset+6] = (char)0x80;
  Buffer[Offset+7] = (char)0x00;
  Buffer[Offset+8] = (char)0x00;
}

void Transport_Packet::SetAdaptationField( ) {
  Buffer[3] = ( Buffer[3] & 0x0F ) + 0x30;
  Buffer[4] = (char)0x07;
  Buffer[5] = (char)0x10;
  Buffer[6] = (char)0x00;
  Buffer[7] = (char)0x00;
  Buffer[8] = (char)0x80;
  Buffer[9] = (char)0xD9;
  Buffer[10] = (char)0x7E;
  Buffer[11] = (char)0x00;
}

void Transport_Packet::SetPayload( char * Payload, int PayloadLen, int Offset ) {
//  std::cerr << "\tSetPayload::Writing " << std::min( PayloadLen, 188-Offset ) << " bytes\n";
  memcpy( &Buffer[Offset], Payload, std::min( PayloadLen, 188-Offset ) );
}


void Transport_Packet::CreatePAT( int ContinuityCounter ) {
  Buffer[3] = (char)0x10 + ContinuityCounter;
  Buffer[4] = (char)0x00;
  Buffer[5] = (char)0x00;
  Buffer[6] = (char)0xD0;
  Buffer[7] = (char)0x0D;
  Buffer[8] = (char)0x00;
  Buffer[9] = (char)0x01;
  Buffer[10] = (char)0xC1;
  Buffer[11] = (char)0x00;
  Buffer[12] = (char)0x00;
  Buffer[13] = (char)0x00;
  Buffer[14] = (char)0x01;
  Buffer[15] = (char)0xF0;
  Buffer[16] = (char)0x00;
  
  Buffer[17] = (char)0x2A;
  Buffer[18] = (char)0xB1;
  Buffer[19] = (char)0x04;
  Buffer[20] = (char)0xB2;
  for(int i = 21; i < 188; i++ ) {
    Buffer[i] = (char)0xFF;
  }
}

void Transport_Packet::CreatePMT( int ContinuityCounter ) {
  Buffer[3] = (char)0x10 + ContinuityCounter;
  Buffer[4] = (char)0x00;
  Buffer[5] = (char)0x02;
  Buffer[6] = (char)0xB0;
  Buffer[7] = (char)0x17;
  Buffer[8] = (char)0x00;
  Buffer[9] = (char)0x01;
  Buffer[10] = (char)0xC1;
  Buffer[11] = (char)0x00;
  Buffer[12] = (char)0x00;
  Buffer[13] = (char)0xE1;
  Buffer[14] = (char)0x00;
  Buffer[15] = (char)0xF0;
  Buffer[16] = (char)0x00;
  Buffer[17] = (char)0x1B;
  Buffer[18] = (char)0xE1;
  Buffer[19] = (char)0x00;
  Buffer[20] = (char)0xF0;
  Buffer[21] = (char)0x00;
  Buffer[22] = (char)0x03;
  Buffer[23] = (char)0xE1;
  Buffer[24] = (char)0x01;
  Buffer[25] = (char)0xF0;
  Buffer[26] = (char)0x00;
  
  Buffer[27] = (char)0x4E;
  Buffer[28] = (char)0x59;
  Buffer[29] = (char)0x3D;
  Buffer[30] = (char)0x1E;
  
  for(int i = 31; i < 188; i++ ) {
    Buffer[i] = (char)0xFF;
  }
}


void SendPAT( Socket::Connection conn ) {
  static int ContinuityCounter = 0;
  Transport_Packet TS;
  TS = Transport_Packet( true, 0 );
  TS.CreatePAT( ContinuityCounter );
  TS.Write( conn );
  ContinuityCounter = ( ContinuityCounter + 1 ) & 0x0F;
}

void SendPMT( Socket::Connection conn ) {
  static int ContinuityCounter = 0;
  Transport_Packet TS;
  TS = Transport_Packet( true, 0x1000 );
  TS.CreatePMT( ContinuityCounter );
  TS.Write( conn );
  ContinuityCounter = ( ContinuityCounter + 1 ) & 0x0F;  
}

std::vector<Transport_Packet> WrapNalus( FLV::Tag tag ) {
  static int ContinuityCounter = 0;
  Transport_Packet TS;
  int PacketAmount = ( ( tag.len - (188 - 25 ) ) / 184 ) + 2;
  std::cerr << "Wrapping a tag of length " << tag.len << " into " << PacketAmount << " TS Packet(s)\n";
  std::vector<Transport_Packet> Result;
  char LeadIn[4] = { (char)0x00, (char)0x00, (char)0x00, (char)0x01 };
  TS = Transport_Packet( true, 0x100 );
  TS.SetContinuityCounter( ContinuityCounter );
  ContinuityCounter = ( ( ContinuityCounter + 1 ) & 0x0F );
  TS.SetAdaptationField( );
  TS.SetPesHeader( 12 );
  TS.SetMessageLength( tag.len - 16 );
  TS.SetPayload( LeadIn, 4, 21 );
  TS.SetPayload( &tag.data[16], 169, 25 );
  Result.push_back( TS );
  for( int i = 0; i < (PacketAmount - 1); i++ ) {
    TS = Transport_Packet( false, 0x100 );
    TS.SetContinuityCounter( ContinuityCounter );
    ContinuityCounter = ( ( ContinuityCounter + 1 ) & 0x0F );
    TS.SetPayload( &tag.data[169+(184*i)], 184, 4 );
    Result.push_back( TS );
  }
  return Result;
}

void Transport_Packet::Write( ) {
  for( int i = 0; i < 188; i++ ) { std::cout << Buffer[i]; }
}

void Transport_Packet::Write( Socket::Connection conn ) {
//  conn.write( Buffer, 188 );
  for( int i = 0; i < 188; i++ ) { std::cout << Buffer[i]; }
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
//              if( firstvideo ) {
//                firstvideo = false;
//              } else {
                SendPAT( conn );
                SendPMT( conn );
                std::vector<Transport_Packet> Meh = WrapNalus( tag );
                std::cerr << "Received " << Meh.size( ) << " Transport Packet(s)\n";
                for( int i = 0; i < Meh.size( ); i++ ) {
                  Meh[i].Write( conn );
                }
//              }
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
