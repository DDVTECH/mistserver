#include "../lib/socket.cpp"
#include <iostream>
#include <unistd.h>

int main(int argc, char **argv){
  Util::printDebugLevel = 10;
  size_t testSize = 4*1024*1024;
  if (argc > 1){
    testSize = atoi(argv[1]);
  }
  std::cout << "About to test " << testSize << " iterations of 8 bytes." << std::endl;
  int p[2];
  if (pipe(p)){
    std::cerr << "Failure: Could not create pipe!" << std::endl;
    return 1;
  }
  Socket::Connection testSock(-1, p[0]);
  char counter[8];
  uint64_t & ctr = *(uint64_t*)counter;
  for (size_t i = 0; i < testSize; ++i){
    ctr = i;
    if (write(p[1], counter, 8) != 8){
      std::cerr << "Failure: Did not write all at " << i << ", aborting test!" << std::endl;
      return 2;
    }
    testSock.spool();
  }
  if (!testSock.Received().available(8*testSize)){
    std::cerr << "Failure: Available bytes mismatch!" << std::endl;
    return 3;
  }
  Util::ResizeablePointer tmp;
  testSock.Received().remove(tmp,8*testSize);

  for (size_t i = 0; i < testSize; ++i){
    if (*(uint64_t*)(((char*)tmp) + i*8) != i){
      std::cerr << "Failure: Data content mismatch at position " << i << "! " << *(uint64_t*)(((char*)tmp) + i*8) << " != " << i << std::endl;
      return 3;
    }
  }


  std::cout << "Success!" << std::endl;
  return 0;
}

