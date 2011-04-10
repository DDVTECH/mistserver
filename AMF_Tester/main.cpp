#define DEBUG 10 //maximum debugging level
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include "../util/amf.h"

int main() {
  std::string temp;
  while (std::cin.good()){temp += std::cin.get();}//read all of std::cin to temp
  temp.erase(temp.size()-1, 1);//strip the invalid last character
  AMF::Object amfdata = AMF::parse(temp);//parse temp into an AMF::Object
  amfdata.Print();//pretty-print the object
  return 0;
}

