#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include "handshake.cpp" //handshaking
#include "parsechunks.cpp" //chunkstream parsing

int main(){
  doHandshake();
  while (!feof(stdin)){
    parseChunk();
  }
  return 0;
}//main
