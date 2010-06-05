#include <iostream>
#include "sockets/SocketW.h"
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>

#define BUFLEN 3000000

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
  int inp_amount;
  int position_current = 0;
  int position_startframe = 0;
  int frame_bodylength = 0;
  SWUnixSocket listener;
  SWUnixSocket *mySocket;
  SWBaseSocket::SWBaseError BError;
  char header[13] = {'F','L','V',0x01,0x05,0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x00};

  listener.bind("../socketfile");
  listener.listen();
  listener.set_timeout(1,0);

  while(true) {
    inp_amount = fread(&input,1,11,stdin);
    if (input[0] == 9) {
      if (!mySocket) {
        mySocket = (SWUnixSocket *)listener.accept(&BError);
        mySocket->send(&header[0],13);
      }
    }
    std::cout << "Blaah\n";
    position_current = 0;
    position_startframe = position_current;
    for(int i = 0; i < 11; i++) { buffer[position_current] = input[i]; position_current ++; }
    frame_bodylength = 0;
    if (machineEndianness() ) {
      frame_bodylength += input[1];
      frame_bodylength += (input[2] << 8);
      frame_bodylength += (input[3] << 16);
    } else {
      frame_bodylength += input[3];
      frame_bodylength += (input[2] << 8);
      frame_bodylength += (input[1] << 16);
    }
    std::cout << frame_bodylength << "\n";
    for (int i = 0; i < frame_bodylength + 4; i++) {
      inp_amount = fread(&input,1,1,stdin);
      buffer[position_current] = input[0]; position_current ++;
      std::cout << " " << position_current << "\n";
    }
    std::cout << "Total message read!\n";
    exit(0);
    mySocket->send(&buffer[0], position_current-1, &BError);
  }

  // disconnect and clean up
  mySocket->disconnect();
  return 0;
}
