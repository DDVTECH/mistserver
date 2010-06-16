#include <iostream>
#include "sockets/SocketW.h"
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include "buffer.h"
#include "user.h"

#define BUFLEN 1000000

int get_empty( user ** list, int amount ) {
  for (int i = 0; i < amount; i++ ){
    if (!list[i]->is_connected()) { return i; }
  }
  return -1;
}

int main( int argc, char * argv[] ) {
  if (argc < 3) { std::cout << "Not the right amount of arguments!\n"; exit(1);}
  int buffers = atoi(argv[1]);
  int total_buffersize = atoi(argv[2]);
  int size_per_buffer = total_buffersize/buffers;
  std::cout << "Size per buffer: " << size_per_buffer << "\n";
  buffer ** ringbuf = (buffer**) calloc (buffers,sizeof(buffer*));
  for (int i = 0; i < buffers; i ++ ) {
    ringbuf[i] = new buffer;
    ringbuf[i]->data = (char*) malloc(size_per_buffer);
  }
  for (int i = 0; i < buffers; i ++ ) { std::cout << "Buffer[" << i << "][0]: " << ringbuf[i]->data[0] << "\n"; }
  int connections = 2;
  user ** connectionList = (user**) calloc (connections,sizeof(user*));
  for (int i = 0; i < connections; i++) { connectionList[i] = new user; }
  char input[BUFLEN];
  char header[BUFLEN];
  int inp_amount;
  int cur_header_pos;
  int position_current = 0;
  int position_startframe = 0;
  int frame_bodylength = 0;
  int current_buffer = 0;
  int open_connection = -1;
  unsigned int loopcount = 0;
  SWUnixSocket listener;
  SWUnixSocket *mySocket = NULL;
  SWBaseSocket::SWBaseError BError;
  cur_header_pos = fread(&header,1,13,stdin);

  listener.bind("/tmp/socketfile");
  listener.listen();
  listener.set_timeout(0,50000);

  while(true) {
    loopcount ++;
    inp_amount = fread(&input,1,11,stdin);
    if (input[0] == 9) {
      open_connection = get_empty(connectionList,connections);
      if (open_connection != -1) {
        connectionList[open_connection]->connect( (SWUnixSocket *)listener.accept(&BError) );
        if (connectionList[open_connection]->is_connected()) {
          std::cout << "#" << loopcount << ": ";
          std::cout << "!!Client " << open_connection << " connected.!!\n";
          connectionList[open_connection]->send_msg(&header[0],13,NULL);
        }
      }
    }
    position_current = 0;
    position_startframe = position_current;
    for(int i = 0; i < 11; i++) { ringbuf[current_buffer]->data[position_current] = input[i]; position_current ++; }
    frame_bodylength = 0;
    frame_bodylength += input[3];
    frame_bodylength += (input[2] << 8);
    frame_bodylength += (input[1] << 16);
    for (int i = 0; i < frame_bodylength + 4; i++) {
      inp_amount = fread(&input,1,1,stdin);
      ringbuf[current_buffer]->data[position_current] = input[0];
      position_current ++;
    }
    ringbuf[current_buffer]->size = position_current;
//    std::cout << "Total message read!\n";
    for (int i = 0; i < connections; i++) {
      if (connectionList[i]->is_connected()) {
//        std::cout << "Client " << i << " connected, sending..\n";
        if ( connectionList[i]->myConnection->send(&ringbuf[current_buffer]->data[0], ringbuf[current_buffer]->size, &BError) == -1) {
          connectionList[i]->disconnect();
          std::cout << "#" << loopcount << ": ";
          std::cout << "!!Client " << i << " disconnected.!!\n";
        }
      }
    }
    current_buffer++;
    current_buffer = current_buffer % buffers;
  }

  // disconnect and clean up
  listener.disconnect();
  return 0;
}
