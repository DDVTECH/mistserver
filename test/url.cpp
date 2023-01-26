#include <mist/url.h>
#include <mist/http_parser.h>
#include <mist/json.h>
#include <iostream>

/// Helper function that compares an environment variable against a string
int checkStr(const char * envVar, const std::string & str){
  //Ignore test when no expected value set
  if (!getenv(envVar)){return 0;}
  //Environment value exists, do check
  if (str != getenv(envVar)){
    //Print error message on mismatch, detailing problem
    std::cerr << "ERROR: Value of " << envVar << " should be '" << getenv(envVar) << "' but was '" << str << "'" << std::endl;
    return 1;
  }
  return 0;
}

/// Helper function that compares an environment variable against an integer
int checkInt(const char * envVar, const uint64_t i){
  //Ignore test when no expected value set
  if (!getenv(envVar)){return 0;}
  //Environment value exists, do check
  if (i != JSON::Value(getenv(envVar)).asInt()){
    //Print error message on mismatch, detailing problem
    std::cerr << "ERROR: Value of " << envVar << " should be '" << getenv(envVar) << "' but was '" << i << "'" << std::endl;
    return 1;
  }
  return 0;
}

int main(int argc, char **argv){
  if (argc < 2){
    std::cout << "Usage: " << argv[0] << " URL" << std::endl;
    return 1;
  }
  HTTP::URL u(argv[1]);
  for (int i = 1; i < argc; ++i){
    HTTP::URL prev = u;
    if (i > 1){u = u.link(argv[i]);}
    std::cout << argv[i] << " -> " << (u.isLocalPath()?u.getFilePath():u.getUrl()) << std::endl;
    if (i > 1){
      std::cout << "Link from previous: " << u.getLinkFrom(prev) << std::endl;
    }
    std::cout << "Proxied URL: " << u.getProxyUrl() << std::endl;
    std::cout << "Protocol: " << u.protocol << std::endl;
    std::cout << "Host: " << u.host << " (Local: " << (Socket::isLocalhost(u.host) ? "Yes" : "No")
              << ")" << std::endl;
    std::cout << "Port: " << u.getPort() << std::endl;
    std::cout << "Path: " << u.path << std::endl;
    std::cout << "Extension: " << u.getExt() << std::endl;
    std::cout << "Query: " << u.args << std::endl;
    std::cout << "Fragment: " << u.frag << std::endl;
    std::cout << "Username: " << u.user << std::endl;
    std::cout << "Password: " << u.pass << std::endl;
    std::cout << std::endl;

  }

  int ret = 0;
  //These checks only run when the environment variable corresponding to them is set
  ret += checkStr("T_PROTO", u.protocol);
  ret += checkStr("T_HOST", u.host);
  ret += checkInt("T_PORT", u.getPort());
  ret += checkStr("T_PATH", u.path);
  ret += checkStr("T_QUERY", u.args);
  ret += checkStr("T_FRAG", u.frag);
  ret += checkStr("T_USER", u.user);
  ret += checkStr("T_PASS", u.pass);
  ret += checkStr("T_EXT", u.getExt());
  ret += checkStr("T_NORM", u.isLocalPath()?u.getFilePath():u.getUrl());
  return ret;
}
