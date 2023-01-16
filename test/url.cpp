#include "../lib/http_parser.cpp"
#include <cassert>
#include <iostream>

int main(int argc, char **argv){
  if (argc < 2){
    std::cout << "Usage: " << argv[0] << " URL" << std::endl;
    return 1;
  }
  HTTP::URL u(argv[1]);
  for (int i = 1; i < argc; ++i){
    if (i > 1){u = u.link(argv[i]);}
    std::cout << argv[i] << " -> " << u.getUrl() << std::endl;
    std::cout << "Protocol: " << u.protocol << std::endl;
    std::cout << "Host: " << u.host << " (Local: " << (Socket::isLocalhost(u.host) ? "Yes" : "No")
              << ")" << std::endl;
    std::cout << "Port: " << u.getPort() << std::endl;
    std::cout << "Path: " << u.path << std::endl;
    std::cout << "Query: " << u.args << std::endl;
    std::cout << "Fragment: " << u.frag << std::endl;
    std::cout << "Username: " << u.user << std::endl;
    std::cout << "Password: " << u.pass << std::endl;
    std::cout << std::endl;

    assert(u.protocol == std::getenv("Protocol"));
    assert(u.host == std::getenv("Host"));
    std::string ulocal;
    Socket::isLocalhost(u.host) ? ulocal = "Yes" : ulocal = "No";
    assert(ulocal == std::getenv("Local"));
    uint16_t uport = 0;
    std::stringstream ss(std::getenv("Port"));
    ss >> uport;
    assert(u.getPort() == uport);
    assert(u.path == std::getenv("Path"));
    assert(u.args == std::getenv("Query"));
    assert(u.frag == std::getenv("Fragment"));
    assert(u.user == std::getenv("Username"));
    assert(u.pass == std::getenv("Password"));

  }
  return 0;
}
