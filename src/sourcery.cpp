///\file sourcery.cpp
///Utility program used for c-string dumping files.
#include <iomanip>
#include <iostream>
#include <fstream>

int main(int argc, char* argv[]){
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <inputFile> <variableName>" << std::endl;
  }
  std::cout << "const char *" << argv[2] << " = " << std::endl << "  \"";
  int i = 0;
  int total = 0;
  std::ifstream inFile(argv[1]);
  while (inFile.good()){
    unsigned char thisChar = inFile.get();
    if (!inFile.good()){break;}
    switch (thisChar){
      //Filter special characters.
      case '\n': std::cout << "\\n"; i += 2; total--; break;
      case '\r': std::cout << "\\r"; i += 2; total--; break;
      case '\t': std::cout << "\\t"; i += 2; total--; break;
      case '\\': std::cout << "\\\\"; i += 2; total --; break;
      case '\"': std::cout << "\\\""; i += 2; total --; break;
      default:
        if (thisChar < 32 || thisChar > 126){
          //Convert to octal.
          std::cout << '\\' << std::oct << std::setw(3) << std::setfill('0') << (unsigned int)thisChar << std::dec;
          i += 4;
        }else{
          std::cout << thisChar;
          i ++;
        }
    }
    if (i >= 80){
      std::cout << "\" \\" << std::endl << "  \"";
      total += i;
      i = 0;
    }
  }
  std::cout << "\";" << std::endl << "unsigned int " << argv[2] << "_len = " << i + total << ";";
  return 0;
}
