#include "../lib/socket.cpp"
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char **argv){
  if (argc < 2){return 1;}
  if (argc > 2){
    std::cout << "Best IPv4 guess:"
              << Socket::resolveHostToBestExternalAddrGuess(argv[1], AF_INET, argv[2]) << std::endl;
    std::cout << "Best IPv6 guess:"
              << Socket::resolveHostToBestExternalAddrGuess(argv[1], AF_INET6, argv[2]) << std::endl;
  }else{
    std::cout << "Best IPv4 guess:" << Socket::resolveHostToBestExternalAddrGuess(argv[1], AF_INET)
              << std::endl;
    std::cout << "Best IPv6 guess:" << Socket::resolveHostToBestExternalAddrGuess(argv[1], AF_INET6)
              << std::endl;
  }
  return 0;
}
