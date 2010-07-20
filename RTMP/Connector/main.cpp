#include <iostream>
#include <cstdlib>
#include <cmath>

struct Handshake_0 {
  char Version;
};//Handshake_0

struct Handshake_1 {
  char Time[4];
  char Zero[4];
  char Random[1528];
};//Handshake_1

struct Handshake_2 {
  char Time[4];
  char Time2[4];
  char Random_Echo[1528];
};//Handshake_2

int main( ) {
 char blaat;
  srand( time( NULL ) );
  Handshake_0 Client_0;
  Handshake_1 Client_1;
  Handshake_1 Server_1;
  Handshake_2 Server_2;
  std::cin >> Client_0.Version;
  std::cin >> Client_1.Time;
  std::cin >> Client_1.Zero;
  std::cin >> Client_1.Random;
  Server_1.Time[0] = 0;
  Server_1.Time[1] = 0;
  Server_1.Time[2] = 0;
  Server_1.Time[3] = 4;
  Server_1.Zero[0] = 0;
  Server_1.Zero[1] = 0;
  Server_1.Zero[2] = 0;
  Server_1.Zero[3] = 0;
  for (int i = 0; i < 1528; i++) {
    Server_1.Random[i] = (rand() % 256);
  }
  std::cout << Client_0.Version;
  std::cout << Server_1.Time[0];
  std::cout << Server_1.Time[1];
  std::cout << Server_1.Time[2];
  std::cout << Server_1.Time[3];
  std::cout << Server_1.Zero[0];
  std::cout << Server_1.Zero[1];
  std::cout << Server_1.Zero[2];
  std::cout << Server_1.Zero[3];
  for (int i = 0; i < 1528; i++) {
    std::cout << Server_1.Random[i];
  }
  for (int i = 0; i < 4; i++ ) {
    Server_2.Time[i] = Client_1.Time[i];
    Server_2.Time2[i] = Server_2.Time[i];
  }
  Server_2.Time2[3] = Server_2.Time2[3] + 1;
  std::cout << Server_2.Time[0];
  std::cout << Server_2.Time[1];
  std::cout << Server_2.Time[2];
  std::cout << Server_2.Time[3];
  std::cout << Server_2.Time2[0];
  std::cout << Server_2.Time2[1];
  std::cout << Server_2.Time2[2];
  std::cout << Server_2.Time2[3];
  for (int i = 0; i < 1528; i++) {
    std::cout << Client_1.Random[i];
  }
  while(std::cin.good()) {
    std::cin >> blaat;
  }
  return 0;
}
