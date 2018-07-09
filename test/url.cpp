#include "../lib/http_parser.cpp"
#include <iostream>

int main(int argc, char ** argv){
  if (argc < 2){
    std::cout << "Usage: " << argv[0] << " URL" << std::endl;
    return 1;
  }
  HTTP::URL u(argv[1]);
  for (int i = 1; i < argc; ++i){
    if (i > 1){
      u = u.link(argv[i]);
    }
    std::cout << argv[i] << " -> " << u.getUrl() << std::endl;
    std::cout << "Protocol: " << u.protocol << std::endl;
    std::cout << "Host: " << u.host << " (Local: " << (Socket::isLocalhost(u.host)?"Yes":"No") << ")" << std::endl;
    std::cout << "Port: " << u.getPort() << std::endl;
    std::cout << "Path: " << u.path << std::endl;
    std::cout << "Query: " << u.args << std::endl;
    std::cout << "Fragment: " << u.frag << std::endl;
    std::cout << "Username: " << u.user << std::endl;
    std::cout << "Password: " << u.pass << std::endl;
    std::cout << std::endl;
  }
  return 0;
}

