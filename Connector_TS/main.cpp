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
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include "../util/socket.h"
#include "../util/flv_tag.h"

class Transport_Packet {
  public:
    Transport_Packet( bool PacketStart = false, int PID = 0x100 );
    void SetPayload( char * Payload, int PayloadLen, int Offset = 13 );
    void SetPesHeader( int Offset = 4, int MsgLen = 0xFFFF, int Current = 0, int Previous = 0 );
    void Write( );
    void Write( Socket::Connection conn );
    void SetContinuityCounter( int Counter );
    void SetMessageLength( int MsgLen );
    void SetAdaptationField( double TimeStamp = 0 );
    void CreatePAT( int ContinuityCounter );
    void CreatePMT( int ContinuityCounter );
  private:
    int PID;
    char Buffer[188];
};//Transport Packet

unsigned int getNowMS() {
  timeval t;
  gettimeofday(&t, 0);
  return t.tv_sec + t.tv_usec/1000;
}

Transport_Packet::Transport_Packet( bool PacketStart, int PID ) {
  (*this).PID = PID;
  Buffer[0] = (char)0x47;
  Buffer[1] = ( PacketStart ? (char)0x40 : 0 ) + (( PID & 0xFF00 ) >> 8 );
  Buffer[2] = ( PID & 0x00FF );
  Buffer[3] = (char)0x10;
}

void Transport_Packet::SetMessageLength( int MsgLen ) {
  Buffer[8] = ( MsgLen & 0xFF00 ) >> 8;
  Buffer[9] = ( MsgLen & 0xFF );
}

void Transport_Packet::SetContinuityCounter( int Counter ) {
  Buffer[3] = ( Buffer[3] & 0xF0 ) + ( Counter & 0x0F );
}

void Transport_Packet::SetPesHeader( int Offset, int MsgLen, int Current, int Previous ) {
  Current = Current * 27000;
  Previous = Previous * 27000;
  Buffer[Offset] = (char)0x00;
  Buffer[Offset+1] = (char)0x00;
  Buffer[Offset+2] = (char)0x01;
  Buffer[Offset+3] = (char)0xE0;
  Buffer[Offset+4] = ( MsgLen & 0xFF00 ) >> 8;
  Buffer[Offset+5] = ( MsgLen & 0x00FF );
  Buffer[Offset+6] = (char)0x80;
  Buffer[Offset+7] = (char)0xC0;
  Buffer[Offset+8] = (char)0x0A;
  Buffer[Offset+9] = (char)0x30 + ( (Current & 0xC0000000 ) >> 29 ) + (char)0x01;
  Buffer[Offset+10] = ( ( ( Current & 0x3FFF8000 ) >> 14 ) & 0xFF00 ) >> 8;
  Buffer[Offset+11] = ( ( ( Current & 0x3FFF8000 ) >> 14 ) & 0x00FF ) + (char)0x01;
  Buffer[Offset+12] = ( ( ( Current & 0x00007FFF ) << 1 ) & 0xFF00 ) >> 8;
  Buffer[Offset+13] = ( ( ( Current & 0x00007FFF ) << 1 ) & 0x00FF ) + (char)0x01;
  Buffer[Offset+14] = (char)0x10 + ( (Previous & 0xC0000000 ) >> 29 ) + (char)0x01;
  Buffer[Offset+15] = ( ( ( Previous & 0x3FFF8000 ) >> 14 ) & 0xFF00 ) >> 8;
  Buffer[Offset+16] = ( ( ( Previous & 0x3FFF8000 ) >> 14 ) & 0x00FF ) + (char)0x01;
  Buffer[Offset+17] = ( ( ( Previous & 0x00007FFF ) << 1 ) & 0xFF00 ) >> 8;
  Buffer[Offset+18] = ( ( ( Previous & 0x00007FFF ) << 1 ) & 0x00FF ) + (char)0x01;
}

void Transport_Packet::SetAdaptationField( double TimeStamp ) {
  TimeStamp = TimeStamp * 27000;
  int Extension = (int)TimeStamp % 300;
  int Base = (int)TimeStamp / 300;
  Buffer[3] = ( Buffer[3] & 0x0F ) + 0x30;
  Buffer[4] = (char)0x07;
  Buffer[5] = (char)0x10;
  Buffer[6] = ( ( Base >> 1 ) & 0xFF000000 ) >> 24;
  Buffer[7] = ( ( Base >> 1 ) & 0x00FF0000 ) >> 16;
  Buffer[8] = ( ( Base >> 1 ) & 0x0000FF00 ) >> 8;
  Buffer[9] = ( ( Base >> 1 ) & 0x000000FF );
  Buffer[10] = ( ( Extension & 0x0100) >> 8 ) + ( ( Base & 0x00000001 ) << 7 ) + (char)0x7E;
  Buffer[11] = ( Extension & 0x00FF);
}

void Transport_Packet::SetPayload( char * Payload, int PayloadLen, int Offset ) {
//  std::cerr << "\tSetPayload::Writing " << std::min( PayloadLen, 188-Offset ) << " bytes\n";
  memcpy( &Buffer[Offset], Payload, std::min( PayloadLen, 188-Offset ) );
}


void Transport_Packet::CreatePAT( int ContinuityCounter ) {
  Buffer[3] = (char)0x10 + ContinuityCounter;
  Buffer[4] = (char)0x00;
  Buffer[5] = (char)0x00;
  Buffer[6] = (char)0xB0;
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
  static int Previous_Tag = 0;
  static int First_PCR = getNowMS();
  static int Previous_PCR = 0;
  int Current_PCR;
  int Current_Tag;

  int Offset = 0;
  Transport_Packet TS;
  int Sent = 0;
  int PacketAmount = ( ( tag.len - (188 - 35 ) ) / 184 ) + 2;
  std::vector<Transport_Packet> Result;
  char LeadIn[4] = { (char)0x00, (char)0x00, (char)0x00, (char)0x01 };
  TS = Transport_Packet( true, 0x100 );
  TS.SetContinuityCounter( ContinuityCounter );
  ContinuityCounter = ( ( ContinuityCounter + 1 ) & 0x0F );
  Current_PCR = getNowMS();
  Current_Tag = ( tag.data[7] << 24 ) + ( tag.data[4] << 16 ) + ( tag.data[5] << 8 ) + ( tag.data[6] );
  if( true ) { //Current_PCR - Previous_PCR >= 1 ) {
    TS.SetAdaptationField( Current_PCR - First_PCR );
    Offset = 8;
    Previous_PCR = Current_PCR;
  }
  TS.SetPesHeader( 4 + Offset, tag.len - 16 , Current_Tag, Previous_Tag );
  Previous_Tag = Current_Tag;
  TS.SetPayload( LeadIn, 31, 23 + Offset );
  TS.SetPayload( &tag.data[16], 157 - Offset, 27 + Offset );
  Sent = 157 - Offset;
  Result.push_back( TS );
  while( Sent + 176 < tag.len - 16 ) {
//  for( int i = 0; i < (PacketAmount - 1); i++ ) {
    TS = Transport_Packet( false, 0x100 );
    TS.SetContinuityCounter( ContinuityCounter );
    ContinuityCounter = ( ( ContinuityCounter + 1 ) & 0x0F );
    Current_PCR = getNowMS();
    Offset = 0;
    if( true ) { //Current_PCR - Previous_PCR >= 1 ) {
      TS.SetAdaptationField( Current_PCR - First_PCR );
      Offset = 8;
      Previous_PCR = Current_PCR;
    }
    TS.SetPayload( &tag.data[16 + Sent], 184 - Offset, 4 + Offset );
    Sent += 184 - Offset;
    Result.push_back( TS );
  }
  if( Sent < ( tag.len - 16 ) ) {
    Current_PCR = getNowMS();
    Offset = 0;
    if( true ) { //now - Previous_PCR >= 5 ) {
      TS.SetAdaptationField( Current_PCR - First_PCR );
      Offset = 8;
      Previous_PCR = Current_PCR;
    }
    std::cerr << "Wrapping packet: last packet length\n";
    std::cerr << "\tTotal:\t\t" << tag.len - 16 << "\n";
    std::cerr << "\tSent:\t\t" << Sent << "\n";
    int To_Send = ( tag.len - 16 ) - Sent;
    std::cerr << "\tTo Send:\t" << To_Send << "\n";
    std::cerr << "\tStuffing:\t" << 176 - To_Send << "\n";
    char Stuffing = (char)0xFF;
    for( int i = 0; i < ( 176 - To_Send ); i++ ) {
      TS.SetPayload( &Stuffing, 1, 4 + Offset + i );
    }
    TS.SetPayload( &tag.data[16 + Sent],  176 - To_Send , 4 + Offset + ( 176 - To_Send ) );
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
  int zet = 0;
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
            if( ( ( tag.data[ 11 ] & 0x0F ) == 7 ) ) { //&& ( tag.data[ 12 ] == 1 ) ) {
              fprintf(stderr, "Video contains NALU\n" );
               // if( firstvideo ) {
               //   firstvideo = false;
               // } else {
                  SendPAT( conn );
                  SendPMT( conn );
                  std::vector<Transport_Packet> Meh = WrapNalus( tag );
                  for( int i = 0; i < Meh.size( ); i++ ) {
                    Meh[i].Write( conn );
                  }
                  std::cerr << "Item: " << ++zet << "\n";
               // }
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


//TODO::TODO::TODO::Fix Timestamps
