#include <iostream>
#include "../sockets/SocketW.h"
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <unistd.h>
#include "../util/flv.cpp" //FLV format parser
#include "user.cpp"

int get_empty( user ** list, int amount ) {
  for (int i = 0; i < amount; i++ ){
    if (!list[i]->is_connected){return i;}
  }
  return -1;
}

int main( int argc, char * argv[] ) {
  if (argc < 2) {
    std::cout << "usage: " << argv[0] << " buffers_count [streamname]" << std::endl;
    return 1;
  }
  int metabuflen = 0;
  char * metabuffer = 0;
  int buffers = atoi(argv[1]);
  buffer ** ringbuf = (buffer**) calloc (buffers,sizeof(buffer*));
  std::vector<user> connectionList;
  std::vector<user>::iterator connIt;
  for (int i = 0; i < buffers; ++i) ringbuf[i] = new buffer;
  int current_buffer = 0;
  int lastproper = 0;//last properly finished buffer number
  unsigned int loopcount = 0;
  SWUnixSocket listener(SWBaseSocket::nonblocking);
  SWBaseSocket * incoming = 0;
  SWBaseSocket::SWBaseError BError;

  std::string shared_socket = "/tmp/shared_socket";
  if (argc > 2){
    shared_socket = argv[2];
    shared_socket = "/tmp/shared_socket_" + shared_socket;
  }
  unlink(shared_socket.c_str());
  listener.bind(shared_socket.c_str());
  listener.listen(50);
  listener.set_timeout(0,50000);
  unsigned char packtype;
  bool gotVideoInfo = false;
  bool gotAudioInfo = false;
  while(std::cin.good()) {
    loopcount ++;
    //invalidate the current buffer
    ringbuf[current_buffer]->number = -1;
    if (std::cin.peek() == 'F') {
      //new FLV file, read the file header again.
      FLV_Readheader();
    } else {
      FLV_GetPacket(ringbuf[current_buffer]->FLV);
      packtype = ringbuf[current_buffer]->FLV->data[0];
      //store metadata, if available
      if (packtype == 0x12){
        metabuflen = ringbuf[current_buffer]->FLV->len;
        metabuffer = (char*)realloc(metabuffer, metabuflen);
        memcpy(metabuffer, ringbuf[current_buffer]->FLV->data, metabuflen);
        std::cout << "Received metadata!" << std::endl;
        gotVideoInfo = false;
        gotAudioInfo = false;
      }
      if (!gotVideoInfo && ringbuf[current_buffer]->FLV->isKeyframe){
        if ((ringbuf[current_buffer]->FLV->data[11] & 0x0f) == 7){//avc packet
          if (ringbuf[current_buffer]->FLV->data[12] == 0){
            ringbuf[current_buffer]->FLV->data[4] = 0;//timestamp to zero
            ringbuf[current_buffer]->FLV->data[5] = 0;//timestamp to zero
            ringbuf[current_buffer]->FLV->data[6] = 0;//timestamp to zero
            metabuffer = (char*)realloc(metabuffer, metabuflen + ringbuf[current_buffer]->FLV->len);
            memcpy(metabuffer+metabuflen, ringbuf[current_buffer]->FLV->data, ringbuf[current_buffer]->FLV->len);
            metabuflen += ringbuf[current_buffer]->FLV->len;
            gotVideoInfo = true;
            std::cout << "Received video configuration!" << std::endl;
          }
        }else{gotVideoInfo = true;}//non-avc = no config...
      }
      if (!gotAudioInfo && (packtype == 0x08)){
        if (((ringbuf[current_buffer]->FLV->data[11] & 0xf0) >> 4) == 10){//aac packet
          ringbuf[current_buffer]->FLV->data[4] = 0;//timestamp to zero
          ringbuf[current_buffer]->FLV->data[5] = 0;//timestamp to zero
          ringbuf[current_buffer]->FLV->data[6] = 0;//timestamp to zero
          metabuffer = (char*)realloc(metabuffer, metabuflen + ringbuf[current_buffer]->FLV->len);
          memcpy(metabuffer+metabuflen, ringbuf[current_buffer]->FLV->data, ringbuf[current_buffer]->FLV->len);
          metabuflen += ringbuf[current_buffer]->FLV->len;
          gotAudioInfo = true;
          std::cout << "Received audio configuration!" << std::endl;
        }else{gotAudioInfo = true;}//no aac = no config...
      }
      //on keyframe set start point
      if (packtype == 0x09){
        if (((ringbuf[current_buffer]->FLV->data[11] & 0xf0) >> 4) == 1){lastproper = current_buffer;}
      }
      incoming = listener.accept(&BError);
      if (incoming){
        connectionList.push_back(user(incoming));
        //send the FLV header
        connectionList.back().MyBuffer = lastproper;
        connectionList.back().MyBuffer_num = -1;
        //TODO: Do this more nicely?
        if (connectionList.back().Conn->send(FLVHeader,13,&BError) != 13){
          connectionList.back().disconnect("failed to receive the header!");
        }else{
          if (connectionList.back().Conn->send(metabuffer,metabuflen,&BError) != metabuflen){
            connectionList.back().disconnect("failed to receive metadata!");
          }
        }
        if (BError != SWBaseSocket::ok){
          connectionList.back().disconnect("Socket error: " + BError.get_error());
        }
      }
      ringbuf[current_buffer]->number = loopcount;
      //send all connections what they need, if and when they need it
      for (connIt = connectionList.begin(); connIt != connectionList.end(); connIt++){
        if (!(*connIt).is_connected){connectionList.erase(connIt);break;}
        (*connIt).Send(ringbuf, buffers);
      }
      //keep track of buffers
      current_buffer++;
      current_buffer %= buffers;
    }
  }

  // disconnect listener
  std::cout << "Reached EOF of input" << std::endl;
  listener.disconnect(&BError);
  return 0;
}
