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
  if (argc < 3) {
    std::cout << "usage: " << argv[0] << " buffers_count max_clients" << std::endl;
    return 1;
  }
  int metabuflen = 0;
  char * metabuffer = 0;
  int buffers = atoi(argv[1]);
  int connections = atoi(argv[2]);
  buffer ** ringbuf = (buffer**) calloc (buffers,sizeof(buffer*));
  user ** connectionList = (user**) calloc (connections,sizeof(user*));
  for (int i = 0; i < buffers; ++i) ringbuf[i] = new buffer;
  for (int i = 0; i < connections; ++i) connectionList[i] = new user;
  int current_buffer = 0;
  int open_connection = -1;
  int lastproper = 0;//last properly finished buffer number
  unsigned int loopcount = 0;
  SWUnixSocket listener(SWBaseSocket::nonblocking);
  SWBaseSocket * incoming = 0;
  SWBaseSocket::SWBaseError BError;

  unlink("/tmp/shared_socket");
  listener.bind("/tmp/shared_socket");
  listener.listen();
  listener.set_timeout(0,50000);

  while(std::cin.good()) {
    loopcount ++;
    //invalidate the current buffer
    ringbuf[current_buffer]->number = -1;
    if (std::cin.peek() == 'F') {
      //new FLV file, read the file header again.
      FLV_Readheader();
    } else {
      FLV_GetPacket(ringbuf[current_buffer]->FLV);
      //if video frame? (id 9) check for incoming connections
      if (ringbuf[current_buffer]->FLV->data[0] == 0x12){
        metabuflen = ringbuf[current_buffer]->FLV->len;
        metabuffer = (char*)realloc(metabuffer, metabuflen);
        memcpy(metabuffer, ringbuf[current_buffer]->FLV->data, metabuflen);
      }
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
          if (connectionList[open_connection]->Conn->send(FLVHeader,13,0) != 13){
            connectionList[open_connection]->disconnect();
            std::cout << "Client " << open_connection << " failed to receive the header!" << std::endl;
          }
          if (connectionList[open_connection]->Conn->send(metabuffer,metabuflen,0) != metabuflen){
            connectionList[open_connection]->disconnect();
            std::cout << "Client " << open_connection << " failed to receive metadata!" << std::endl;
          }
          std::cout << "Client " << open_connection << " received metadata and header!" << std::endl;
        }else{
          std::cout << "New client not connected: no more connections!" << std::endl;
          incoming->disconnect();
        }
      }
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
  std::cout << "Reached EOF of input" << std::endl;
  listener.disconnect(&BError);
  return 0;
}
