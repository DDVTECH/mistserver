///\file sourcery.cpp
///Utility program used for c-string dumping files.
#include <stdint.h>
#include <iomanip>
#include <iostream>
#include <fstream>

int main(int argc, char* argv[]){
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0] << " <inputFile> <variableName> <outputFile>" << std::endl;
    return 42;
  }
  std::ofstream tmp(argv[3]);
  //begin the first line
  tmp << "const char *" << argv[2] << " = " << std::endl << "  \"";
  uint32_t i = 0; //Current line byte counter
  uint32_t total = 0; //Finished lines so far byte counter
  std::ifstream inFile(argv[1]);
  bool sawQ = false;
  while (inFile.good()){
    unsigned char thisChar = inFile.get();
    if (!inFile.good()){break;}
    switch (thisChar){
      //Filter special characters.
      case '\n': tmp << "\\n";  break;
      case '\r': tmp << "\\r";  break;
      case '\t': tmp << "\\t";  break;
      case '\\': tmp << "\\\\"; break;
      case '\"': tmp << "\\\""; break;
      case '?':
        if (sawQ){tmp << "\"\"";}
        tmp << "?";
        sawQ = true;
      break;
      default:
        if (thisChar < 32 || thisChar > 126){
          //Convert to octal.
          tmp << '\\' << std::oct << std::setw(3) << std::setfill('0') << (unsigned int)thisChar << std::dec;
        }else{
          tmp << thisChar;
        }
        sawQ = false;
    }
    ++i;
    // We print 80 bytes per line, regardless of special characters
    // (Mostly because calculating this correctly would double the lines of code for this utility -_-)
    if (i >= 80){
      tmp << "\" \\" << std::endl << "  \"";
      total += i;
      i = 0;
    }
  }
  //end the last line, plus length variable
  tmp << "\";" << std::endl << "uint32_t " << argv[2] << "_len = " << i + total << ";" << std::endl;
  tmp.close();
  return 0;
}

