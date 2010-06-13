#include <iostream>
#include "sockets/SocketW.h"
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include "buffer.h"

#define BUFLEN 1000000

int main( int argc, char * argv[] ) {
  if (argc != 3) { std::cout << "Not the right amount of arguments!\n"; exit(1);}
  int buffers = atoi(argv[1]);
  int total_buffersize = atoi(argv[2]);
  int size_per_buffer = total_buffersize/buffers;
  std::cout << "Size per buffer: " << size_per_buffer << "\n";
  buffer ** ringbuf = (buffer**) calloc (buffers,sizeof(buffer*));
  for (int i = 0; i < buffers; i ++ ) {
    ringbuf[i] = new buffer;
    ringbuf[i]->data = (char*) malloc(size_per_buffer);
    ringbuf[i]->data[0] = i+'a';
  }
  for (int i = 0; i < buffers; i ++ ) {
    std::cout << "Buffer[" << i << "][0]: " << ringbuf[i]->data[0] << "\n";
  }
  char input[BUFLEN];
  char header[BUFLEN];
  int inp_amount;
  int cur_header_pos;
  int position_current = 0;
  int position_startframe = 0;
  int frame_bodylength = 0;
  int current_buffer = 0;
  SWUnixSocket listener;
  SWUnixSocket *mySocket = NULL;
  SWBaseSocket::SWBaseError BError;
  cur_header_pos = fread(&header,1,13,stdin);

  listener.bind("/tmp/socketfile");
  listener.listen();
  listener.set_timeout(1,0);

  while(true) {
    inp_amount = fread(&input,1,11,stdin);
    if (input[0] == 9) {
      std::cout << "9!!\n";
      if (!mySocket) {
        mySocket = (SWUnixSocket *)listener.accept(&BError);
        if (mySocket) {
          mySocket->send(&header[0],13);
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

    std::cout << frame_bodylength << "\n";
    for (int i = 0; i < frame_bodylength + 4; i++) {
      inp_amount = fread(&input,1,1,stdin);
      ringbuf[current_buffer]->data[position_current] = input[0];
      position_current ++;
    }
    std::cout << "Total message read!\n";
    if (mySocket) {
      std::cout << "  mySocket: " << mySocket << "\n";
      if ( mySocket->fsend(&ringbuf[current_buffer]->data[0], position_current, &BError) == -1) {
        mySocket->disconnect();
        mySocket->close_fd();
        std::cout << "Disconnected, closed..." << "\n";
        mySocket = 0;
      }
    }
    current_buffer++;
    current_buffer = current_buffer % buffers;
  }

  // disconnect and clean up
  listener.disconnect();
  return 0;
}
