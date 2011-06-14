#include <iostream>
#include <queue>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <getopt.h>
#include <ctime>
#include <string>
#include <map>
#include <cerrno>
#include "../../../util/ddv_socket.h"
#include "../../../util/md5.h"
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#define USER_ID "5"
#define USER_LOGIN "Chocokoekjes"

std::string Decode( std::string input, std::string XorPath ) {
  static int counter = 0;
  std::string Result;
  for( unsigned int i = 0; i < input.size( ); i ++) {
    Result.push_back( (char)( input[i] ^ XorPath[counter] ) );
    counter = ( counter + 1 ) % XorPath.size( );
  }
  return Result;
}

std::string Encode( std::string input, std::string XorPath ) {
  static int counter = 0;
  std::string Result;
  for( unsigned int i = 0; i < input.size( ); i ++) {
    Result.push_back( (char)( input[i] ^ XorPath[counter] ) );
    counter = ( counter + 1 ) % XorPath.size( );
  }
  return Result;
}

int main( ) {
  std::string temp;
  std::string random;
  std::string xorpath;
  std::string md5th;
  errno = 0;
  int sockno = socket( AF_INET, SOCK_STREAM, 0 );
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(7337);
  inet_aton( "0.0.0.0", &(addr.sin_addr));
  if( connect( sockno, (sockaddr*)&addr, sizeof(sockaddr_in) ) ) {
    printf( "ERROR::Connection failed: %d\n", errno == ECONNREFUSED);
    exit( 1 );
  }
  DDV::Socket Sock( sockno );
  while( temp.find('\n') == std::string::npos ) {
    Sock.read( temp );
  }
  random = temp.substr(7,8);
  md5th = md5( random + USER_LOGIN + random );
  std::cout << "REC::\t" << temp.substr(0,temp.find('\n')) << "\n";
  std::cout << "\t" << random << "\n";
  std::cout << "SND::\t" << "OCC" << USER_ID << ":" << md5th << "\n";
  temp = "OCC";
  temp += USER_ID;
  temp += ":";
  temp += md5th;
  temp += "\n";
  Sock.write( temp );
  temp = "";
  while( temp.find('\n') == std::string::npos ) {
    Sock.read( temp );
  }
  std::cout << "REC::\t" << temp.substr(0,temp.find('\n')) << "\n";
  xorpath = md5( temp.substr(3,8) + USER_LOGIN + temp.substr(3,8) );
  std::cout << "\tCalculated xorpath: " << xorpath << "\n";
  while( Sock.connected( ) ) {
    std::cout << "SND::";
    std::cin >> temp;
    Sock.write( Encode( temp + "\n", xorpath ) );
    temp = "";
    if( Sock.connected( ) && Sock.ready( ) ) {
      Sock.read( temp );
      std::cout << "REC::" << Decode(temp,xorpath) << "\n";
    }
  }
  Sock.close( );
  return 0;
}
