///\file sourcery.cpp
///Utility program used for c-string dumping files.
#include <iomanip>
#include <iostream>
#include <fstream>

int main(int argc, char* argv[]){
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0] << " <inputFile> <variableName> <outputFile>" << std::endl;
  }
  std::ofstream tmp(argv[3]);
  tmp << "const char *" << argv[2] << " = " << std::endl << "  \"";
  int i = 0;
  int total = 0;
  std::ifstream inFile(argv[1]);
  while (inFile.good()){
    unsigned char thisChar = inFile.get();
    if (!inFile.good()){break;}
    switch (thisChar){
      //Filter special characters.
      case '\n': tmp << "\\n"; i += 2; total--; break;
      case '\r': tmp << "\\r"; i += 2; total--; break;
      case '\t': tmp << "\\t"; i += 2; total--; break;
      case '\\': tmp << "\\\\"; i += 2; total --; break;
      case '\"': tmp << "\\\""; i += 2; total --; break;
      default:
        if (thisChar < 32 || thisChar > 126){
          //Convert to octal.
          tmp << '\\' << std::oct << std::setw(3) << std::setfill('0') << (unsigned int)thisChar << std::dec;
          i += 4;
        }else{
          tmp << thisChar;
          i ++;
        }
    }
    if (i >= 80){
      tmp << "\" \\" << std::endl << "  \"";
      total += i;
      i = 0;
    }
  }
  tmp << "\";" << std::endl << "unsigned int " << argv[2] << "_len = " << i + total << ";" << std::endl;
  tmp.close();
  return 0;
}
