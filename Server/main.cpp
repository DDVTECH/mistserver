#include <iostream>
#include "sockets/SocketW.h"
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include "buffer.h"
#include "user.h"
#include "string.h"

#define BUFLEN 1000000

int get_empty( user ** list, int amount ) {
  for (int i = 0; i < amount; i++ ){
    if (!list[i]->is_connected){return i;}
  }
  return -1;
}

int main( int argc, char * argv[] ) {
  if (argc < 4) {
    std::cout << "usage: " << argv[0] << " buffers_count total_buffersize max_clients" << std::endl;
    return 1;
  }
  int buffers = atoi(argv[1]);
  int total_buffersize = atoi(argv[2]);
  int connections = atoi(argv[3]);
  int size_per_buffer = total_buffersize/buffers;
  std::cout << "Size per buffer: " << size_per_buffer << std::endl;
  buffer ** ringbuf = (buffer**) calloc (buffers,sizeof(buffer*));
  for (int i = 0; i < buffers; i ++ ) {
    ringbuf[i] = new buffer;
    ringbuf[i]->data = (char*) malloc(size_per_buffer);
  }
  std::cout << "Successfully allocated " << total_buffersize << " bytes total buffer." << std::endl;
  user ** connectionList = (user**) calloc (connections,sizeof(user*));
  for (int i = 0; i < connections; i++) { connectionList[i] = new user; }
  char header[13];//FLV header is always 13 bytes
  char metadata[BUFLEN];
  int ret = 0;
  int frame_bodylength = 0;
  int current_buffer = 0;
  int open_connection = -1;
  int lastproper = 0;//last properly finished buffer number
  unsigned int loopcount = 0;
  SWUnixSocket listener;
  SWBaseSocket * incoming = 0;
  SWBaseSocket::SWBaseError BError;
  //read FLV header - 13 bytes
  ret = fread(&header,1,13,stdin);
  //TODO: check ret?

  listener.bind("/tmp/socketfile");
  listener.listen();
  listener.set_timeout(0,50000);
  
  //TODO: not while true, but while running - set running to false when kill signal is received!
  while(true) {
    loopcount ++;
    //invalidate the current buffer
    ringbuf[current_buffer]->size = 0;
    ringbuf[current_buffer]->number = -1;
    if (std::cin.peek() == 'F') {
      //new FLV file, read the file header again.
      ret = fread(&header,1,13,stdin);
    } else if( (int)std::cin.peek() == 12) {
      //metadata encountered, let's filter it
      ret = fread(&metadata,1,11,stdin);
      frame_bodylength = 4;
      frame_bodylength += metadata[3];
      frame_bodylength += (metadata[2] << 8);
      frame_bodylength += (metadata[1] << 16);
      ret = fread(&metadata,1,frame_bodylength,stdin);
    } else {
      //read FLV frame header - 11 bytes
      ret = fread(ringbuf[current_buffer]->data,1,11,stdin);
      //TODO: Check ret?
      //if video frame? (id 9) check for incoming connections
      if (ringbuf[current_buffer]->data[0] == 9) {
        incoming = listener.accept(&BError);
        if (incoming){
          open_connection = get_empty(connectionList,connections);
          if (open_connection != -1) {
            connectionList[open_connection]->connect(incoming);
            //send the FLV header
            std::cout << "Client " << open_connection << " connected." << std::endl;
            connectionList[open_connection]->MyBuffer = lastproper;
            connectionList[open_connection]->MyBuffer_num = ringbuf[lastproper]->number;
            //TODO: Do this more nicely?
            if (connectionList[open_connection]->Conn->send(&header[0],13,NULL) != 13){
              connectionList[open_connection]->disconnect();
              std::cout << "Client " << open_connection << " failed to receive the header!" << std::endl;
            }
            std::cout << "Client " << open_connection << " received header!" << std::endl;
          }else{
            std::cout << "New client not connected: no more connections!" << std::endl;
          }
        }
      }
      //calculate body length of frame
      frame_bodylength = 4;
      frame_bodylength += ringbuf[current_buffer]->data[3];
      frame_bodylength += (ringbuf[current_buffer]->data[2] << 8);
      frame_bodylength += (ringbuf[current_buffer]->data[1] << 16);
      //read the rest of the frame
      ret = fread(&ringbuf[current_buffer]->data[11],1,frame_bodylength,stdin);
      //TODO: Check ret?
      ringbuf[current_buffer]->size = frame_bodylength + 11;
      ringbuf[current_buffer]->number = loopcount;
      //send all connections what they need, if and when they need it
      for (int i = 0; i < connections; i++) {connectionList[i]->Send(ringbuf, buffers);}
      //keep track of buffers
      lastproper = current_buffer;
      current_buffer++;
      current_buffer %= buffers;
    }
  }

  // disconnect listener
  listener.disconnect();
  //TODO: cleanup
  return 0;
}
