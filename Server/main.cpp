#include <iostream>
#include "sockets/SocketW.h"
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>

#define BUFLEN 1000000

bool machineEndianness() {
  int i = 1;
  char *p = (char *) &i;
  if (p[0] == 1) {
    return false;//little-endian
  } else {
    return true;
  }
}

int main() {
  char buffer[BUFLEN];
  char input[BUFLEN];
  char header[BUFLEN];
  int inp_amount;
  int cur_header_pos;
  int position_current = 0;
  int position_startframe = 0;
  int frame_bodylength = 0;
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
    for(int i = 0; i < 11; i++) { buffer[position_current] = input[i]; position_current ++; }
    frame_bodylength = 0;
    frame_bodylength += input[3];
    frame_bodylength += (input[2] << 8);
    frame_bodylength += (input[1] << 16);

    std::cout << frame_bodylength << "\n";
    for (int i = 0; i < frame_bodylength + 4; i++) {
      inp_amount = fread(&input,1,1,stdin);
      buffer[position_current] = input[0];
      position_current ++;
    }
    std::cout << "Total message read!\n";
    if (mySocket) {
      mySocket->send(&buffer[0], position_current, &BError);
      if ( BError != SWBaseSocket::ok ) {
        mySocket = 0;
      }
    }
  }

  // disconnect and clean up
  mySocket->disconnect();
  return 0;
}
