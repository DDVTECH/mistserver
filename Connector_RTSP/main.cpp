#define DEBUG(args...) //debugging disabled
//#define DEBUG(args...) fprintf(stderr, args) //debugging enabled

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <ctype.h>

//needed for select
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/time.h>
#include <arpa/inet.h>

unsigned int getNowMS(){
  timeval t;
  gettimeofday(&t, 0);
  return t.tv_sec + t.tv_usec/1000;
}
void hexdump(void *pAddressIn, long  lSize)
{
 char szBuf[100];
 long lIndent = 1;
 long lOutLen, lIndex, lIndex2, lOutLen2;
 long lRelPos;
 struct { char *pData; unsigned long lSize; } buf;
 unsigned char *pTmp,ucTmp;
 unsigned char *pAddress = (unsigned char *)pAddressIn;

   buf.pData   = (char *)pAddress;
   buf.lSize   = lSize;

   while (buf.lSize > 0)
   {
      pTmp     = (unsigned char *)buf.pData;
      lOutLen  = (int)buf.lSize;
      if (lOutLen > 16)
          lOutLen = 16;

      // create a 64-character formatted output line:
      sprintf(szBuf, " >                            "
                     "                      "
                     "    %08lX", pTmp-pAddress);
      lOutLen2 = lOutLen;

      for(lIndex = 1+lIndent, lIndex2 = 53-15+lIndent, lRelPos = 0;
          lOutLen2;
          lOutLen2--, lIndex += 2, lIndex2++
         )
      {
         ucTmp = *pTmp++;

         sprintf(szBuf + lIndex, "%02X ", (unsigned short)ucTmp);
         if(!isprint(ucTmp))  ucTmp = '.'; // nonprintable char
         szBuf[lIndex2] = ucTmp;

         if (!(++lRelPos & 3))     // extra blank after 4 bytes
         {  lIndex++; szBuf[lIndex+2] = ' '; }
      }

      if (!(lRelPos & 3)) lIndex--;

      szBuf[lIndex  ]   = '<';
      szBuf[lIndex+1]   = ' ';

      DEBUG("%s\n", szBuf);

      buf.pData   += lOutLen;
      buf.lSize   -= lOutLen;
   }
}

//for connection to server
#include "../sockets/SocketW.h"
bool ready4data = false;//set to true when streaming starts
bool inited = false;
bool stopparsing = false;
timeval lastrec;

#include "../util/flv_sock.cpp" //FLV parsing with SocketW
#include "rtsp.cpp"
#include "rtp.h"

void parse_ip(char* ip_addr, uint8_t * uitvoer) {
  int i = 0;
  int j = 0;
  int tempint = 0;
  while( ip_addr[i] != '\0' ) {
    if( ip_addr[i] != '.' ) {
      tempint = tempint * 10;
      tempint += ip_addr[i] - '0';
    } else {
      uitvoer[j] = tempint;
      tempint = 0;
      j++;
    }
    i++;
  }
  uitvoer[j] = tempint;
}

int main(){
  unsigned int ts;
  unsigned int fts = 0;
  unsigned int ftst;
  SWUnixSocket ss;
  fd_set pollset;
  struct timeval timeout;
  RTPSession rtp_connection;
  RTPSessionParams sessionparams;
  RTPUDPv4TransmissionParams transparams;

  //0 timeout - return immediately after select call
  timeout.tv_sec = 1; timeout.tv_usec = 0;
  FD_ZERO(&pollset);//clear the polling set
  FD_SET(0, &pollset);//add stdin to polling set


  DEBUG("Starting processing...\n");
  while (!feof(stdin) && !stopparsing){
    //select(1, &pollset, 0, 0, &timeout);
    //only parse input from stdin if available or not yet init'ed
    //FD_ISSET(0, &pollset) || //NOTE: Polling does not work? WHY?!? WHY DAMN IT?!?
    if (!ready4data && !stopparsing){ParseRTSPPacket();}
    if (ready4data && !stopparsing){
      if (!inited){
        //we are ready, connect the socket!
        if (!ss.connect("/tmp/shared_socket")){
          DEBUG("Could not connect to server!\n");
          return 0;
        }
        FLV_Readheader(ss);//read the header, we don't want it
        DEBUG("Header read, starting to send video data...\n");

        sessionparams.SetOwnTimestampUnit(1.0);//1sample/second, dummydata, wordt toch elke keer per packet ingesteld.
        transparams.SetPortbase(serverport);
        rtp_connection.Create(sessionparams,&transparams);

  struct sockaddr_in i;
  int len;
  char * addr1;
  len = sizeof(i);
  if (getpeername( fileno(stdin), (sockaddr*)&i, (socklen_t*)&len ) == 0) {
    addr1 = (char *) inet_ntoa(i.sin_addr);
    DEBUG("%s\n", addr1 );
  } else {
    DEBUG("Blaah\n");
  }

  uint8_t clientip[4];
  parse_ip(addr1,clientip);

        //TODO: clientip ophalen uit stdin-socket: zie http://www.mail-archive.com/plug@lists.q-linux.com/msg16482.html
        RTPIPv4Address addr(clientip,clientport);
        DEBUG("RTP Connected\n");

        inited = true;
      }
      if (FLV_GetPacket(ss)){//able to read a full packet?
        //gooi de time uit de flv codec naar de juiste offsets voor deze kijker
        ts = FLVbuffer[7] * 256*256*256;
        ts += FLVbuffer[4] * 256*256;
        ts += FLVbuffer[5] * 256;
        ts += FLVbuffer[6];
        if (ts != 0){
          if (fts == 0){fts = ts;ftst = getNowMS();}
          ts -= fts;
          FLVbuffer[7] = ts / (256*256*256);
          FLVbuffer[4] = ts / (256*256);
          FLVbuffer[5] = ts / 256;
          FLVbuffer[6] = ts % 256;
          ts += ftst;
        }else{
          ftst = getNowMS();
          FLVbuffer[7] = ftst / (256*256*256);
          FLVbuffer[4] = ftst / (256*256);
          FLVbuffer[5] = ftst / 256;
          FLVbuffer[6] = ftst % 256;
        }

        if( FLVbuffer[0] != 0x12 ) {//Metadata direct filteren.
          if( FLVbuffer[0] == 0x08 ) { //Audio Packet
//            DEBUG("Audio Packet\n");
            rtp_connection.SetTimestampUnit(1.0/11025);//11025 samples/second
            //Audiodata heeft na de flv-tag nog 2 UI8 aan beschrijvingen die NIET bij de AAC-data horen
            //NOTE:Same als hieronder, wat moeten we doen met init-data van aac? die info wordt nu omitted.
            rtp_connection.SendPacket( &FLVbuffer[13], FLV_len - 17, 99, false, 1);
          } else if ( FLVbuffer[0] == 0x09 ) { //Video Packet
//            DEBUG("Video Packet:      %i\n", (FLVbuffer[16] & 0x1F) );
            rtp_connection.SetTimestampUnit(1.0/90000);//90000 samples/second
            //Videodata heeft na de flv-tag nog 2 UI8 en een SI24 aan beschrijvingen die niet bij de NALU horen
            //NOTE:Moeten we eigenlijk wat adobe genereert als sequence headers/endings ook gwoon doorsturen? gebeurt nu wel
            rtp_connection.SendPacket( &FLVbuffer[16], FLV_len - 19, 98, false, 1);
//	    hexdump(&FLVbuffer[16], FLV_len-19 );
          }
        } else {//Datatype 0x12 = metadata, zouden we voor nu weggooien
          DEBUG("Metadata, throwing away\n");
        }
        FLV_Dump();//dump packet and get ready for next
      }
      if ((SWBerr != SWBaseSocket::ok) && (SWBerr != SWBaseSocket::notReady)){
        DEBUG("No more data! :-(  (%s)\n", SWBerr.get_error().c_str());
        return 0;//no more input possible! Fail immediately.
      }
    }
  }
  DEBUG("User disconnected.\n");
  return 0;
}//main
