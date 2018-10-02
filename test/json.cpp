#include "../lib/json.cpp"
#include <iostream>
#include <string>
#include <sstream>

int main(int argc, char ** argv){
  std::string line;
  std::stringstream jData;
  while (getline(std::cin, line)){jData << line;}
  JSON::Value J = JSON::fromString(jData.str().data(), jData.str().size());
  std::cout << J.toPrettyString() << std::endl;
  return 0;
}

