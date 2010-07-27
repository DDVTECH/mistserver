#include <iostream>
#include <cstdlib>
#include <cstdio>
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
  srand( time( NULL ) );
  char next;
  char chunk_header[14];//Space to any chunk header imaginable. Basic header max 3, Chunck msg header max 11
  char extended_timestamp[4];//Not always used, so not included in header space.
  int chunkIDlenght;
  int chunkstream_id;
  Handshake_0 Client_0;
  Handshake_1 Client_1;
  Handshake_2 Client_2;
  Handshake_1 Server_1;
  Handshake_2 Server_2;
  /** Start Handshake **/
  
  /** Read C0 **/
  std::cin >> Client_0.Version;
  /** Read C1 **/
  fread( Client_1.Time, 1, 4, stdin);
  fread( Client_1.Zero, 1, 4, stdin);
  fread( Client_1.Random, 1, 1528, stdin);
  /** Build S1 Packet **/
  Server_1.Time[0] = 0; Server_1.Time[1] = 0; Server_1.Time[2] = 0; Server_1.Time[3] = 4;
  Server_1.Zero[0] = 0; Server_1.Zero[1] = 0; Server_1.Zero[2] = 0; Server_1.Zero[3] = 0;
  for (int i = 0; i < 1528; i++) { Server_1.Random[i] = (rand() % 256); }
  /** Send S0 **/
  std::cout << Client_0.Version;
  /** Send S1 **/
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
  /** Flush output, just for certainty **/
  std::cout << std::flush;
  /** Build S2 Packet **/
  for (int i = 0; i < 4; i++ ) {
    Server_2.Time[i] = Client_1.Time[i];
    Server_2.Time2[i] = Server_2.Time[i];
  }
  Server_2.Time2[3] = Server_2.Time2[3] + 1;
  /** Send S2 **/
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
  /** Flush, necessary in order to work **/
  std::cout << std::flush;
  /** Read C2 **/
  fread( Client_2.Time, 1, 4, stdin);
  fread( Client_2.Time2, 1, 4, stdin);
  fread( Client_2.Random_Echo, 1, 1528, stdin);

  /** Handshake done, continue with connect command **/

  int chunkIDlength = 0;
  next = std::cin.peek();
  if (next > 63 || next == 2) { exit(1); }//Connect command has a 11 byte header, ALWAYS, maximum value of first byte is then 63
  if (next <= 63 && next >= 3) {//1 byte chunkstream ID
    fread ( chunk_header, 1, 12, stdin );
    chunkIDlength = 1;
  } else if ((int) next == 0 ) {//2 bytes chunkstream ID
    fread ( chunk_header, 1, 13, stdin );
    chunkIDlength = 2;
  } else if ((int) next == 1 ) {//3 bytes chunkstream ID
    fread ( chunk_header, 1, 14, stdin );
    chunkIDlength = 3;
  }
  switch(chunkIDlength) {
    case 1: chunkstream_id = (int)chunk_header[0]; break;
    case 2: chunkstream_id = (int)chunk_header[1] + 64; break;
    case 3: chunkstream_id = ((int)chunk_header[2] * 256) + (int)chunk_header[1] + 64; break;
    default: exit(1); break;//Something went wrong
  }

  if ( chunk_header[chunkIDlength] == 0xFF && chunk_header[chunkIDlength+1] == 0xFF && chunk_header[chunkIDlength+2] == 0xFF ) {
    fread ( extended_timestamp, 1, 4, stdin); //read extended timestamp if 3-byte timestamp equals 0xFFFFFF
  } else {//set extended timestamp to 0
    extended_timestamp[0] = 0; extended_timestamp[1] = 0; extended_timestamp[2] = 0; extended_timestamp[3] = 0;
  }

  
  return 0;
}
