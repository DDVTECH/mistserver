#include <iostream>
#include <queue>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <cmath>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <getopt.h>
#include <ctime>
#include "../util/socket.h"
#include "../util/flv_tag.h"

class Transport_Packet {
  public:
    Transport_Packet( int PID = 0x100 );
    void SetPesHeader( );
    void Write( );
  private:
    int PID;
    char Buffer[188];
};//Transport Packet

Transport_Packet::Transport_Packet( int PID ) {
  (*this).PID = PID;
  Buffer[0] = (char)0x47;
  Buffer[1] = (char)0x40 + (( PID & 0xFF00 ) >> 8 );
  Buffer[2] = ( PID & 0x00FF );
  Buffer[3] = (char)0x00;
}

void Transport_Packet::SetPesHeader( ) {
  Buffer[4] = (char)0x00;
  Buffer[5] = (char)0x00;
  Buffer[6] = (char)0x01;
  Buffer[7] = (char)0xE0;
  
  Buffer[10] = (char)0x80;
}

void Transport_Packet::Write( ) {
  for( int i = 0; i < 188; i++ ) { std::cout << Buffer[i]; }
}

std::string WrapNaluIntoTS( char * Buffer, int BufLen ) {
  std::string result;
  return result;
}

int TS_Handler( Socket::Connection conn ) {
  FLV::Tag tag;///< Temporary tag buffer for incoming video data.
  bool ready4data = false;///< Set to true when streaming is to begin.
  bool inited = false;
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
              fprintf(stderr, "Video contains NALU" );
            }
          }
          if( tag.data[ 0 ] == 0x08 ) {
            fprintf(stderr, "Audio Tag Read\n");
          }          
        }
        break;
    }
  }
  Transport_Packet TS = Transport_Packet( );
  TS.SetPesHeader( );
  TS.Write( );
  return 0;
}

#define DEFAULT_PORT 8888
#define MAINHANDLER TS_Handler
#define CONFIGSECT TS
#include "../util/server_setup.cpp"
