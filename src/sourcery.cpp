///\file sourcery.cpp
/// Utility program used for c-string dumping files.
#undef DEBUG
#define DEBUG -1
#include "../lib/encode.cpp"
#include "../lib/encode.h"
#include "../lib/url.cpp"
#include "../lib/url.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdint.h>
#include <string.h>
#include <string>
#include <sstream>
#include <unistd.h>

std::string getContents(const char *fileName){
  std::ifstream inFile(fileName);
  std::string fullText;
  if (inFile){
    std::stringstream contents;
    contents << inFile.rdbuf();
    inFile.close();
    return contents.str();
  }
  return "";
}

int main(int argc, char *argv[]){

  if (argc < 4){
    std::cerr << "Usage: " << argv[0] << " <inputFile> <variableName> <outputFile> [<splittext>]" << std::endl;
    return 42;
  }
  const char *splitText = 0;
  if (argc >= 5){splitText = argv[4];}

  char workDir[512];
  getcwd(workDir, 512);
  HTTP::URL inUri(std::string("file://") + workDir + "/");
  inUri = inUri.link(argv[1]);

  // Read the entire first argument into a string buffer
  std::string fullText = getContents(inUri.getFilePath().c_str());

  // replace every <script src="*"></script> with the contents of the file '*'
  while (fullText.find("<script src=\"") != std::string::npos){
    size_t locStart = fullText.find("<script src=\"");
    size_t locEnd = fullText.find("\"></script>", locStart);
    // Assume we should abort if the strlen of the filename is > 230 chars
    if (locEnd - locStart >= 230){break;}
    HTTP::URL fileName = inUri.link(fullText.substr(locStart + 13, locEnd - locStart - 13));
    std::string subText = getContents(fileName.getFilePath().c_str());
    fullText = fullText.substr(0, locStart) + "<script>" + subText + fullText.substr(locEnd + 2);
  }

  // replace every <link rel="stylesheet" href="*"> with the contents of the file '*'
  while (fullText.find("<link rel=\"stylesheet\" href=\"") != std::string::npos){
    size_t locStart = fullText.find("<link rel=\"stylesheet\" href=\"");
    size_t locEnd = fullText.find("\">", locStart);
    // Assume we should abort if the strlen of the filename is > 230 chars
    if (locEnd - locStart >= 230){break;}
    HTTP::URL fileName = inUri.link(fullText.substr(locStart + 29, locEnd - locStart - 29));
    std::string subText = getContents(fileName.getFilePath().c_str());
    fullText = fullText.substr(0, locStart) + "<style>" + subText + "</style>" + fullText.substr(locEnd + 2);
  }

  size_t splitPoint = std::string::npos;
  size_t splitLen = 0;
  if (splitText){
    splitPoint = fullText.find(splitText);
    if (splitPoint != std::string::npos){splitLen = strlen(splitText);}
  }

  std::ofstream tmp(argv[3]);
  // begin the first line
  if (!splitLen){
    tmp << "const char *" << argv[2] << " = " << std::endl << "  \"";
  }else{
    tmp << "const char *" << argv[2] << "_prefix = " << std::endl << "  \"";
  }
  uint32_t i = 0;     // Current line byte counter
  uint32_t total = 0; // Finished lines so far byte counter
  bool sawQ = false;
  for (size_t pos = 0; pos < fullText.size(); ++pos){
    if (pos == splitPoint){
      tmp << "\";" << std::endl
          << "uint32_t " << argv[2] << "_prefix_len = " << i + total << ";" << std::endl;
      tmp << "const char *" << argv[2] << "_suffix = " << std::endl << "  \"";
      i = 0;
      total = 0;
      sawQ = false;
      pos += splitLen;
    }
    unsigned char thisChar = fullText.at(pos);
    switch (thisChar){
    // Filter special characters.
    case '\n': tmp << "\\n"; break;
    case '\r': tmp << "\\r"; break;
    case '\t': tmp << "\\t"; break;
    case '\\': tmp << "\\\\"; break;
    case '\"': tmp << "\\\""; break;
    case '?':
      if (sawQ){tmp << "\"\"";}
      tmp << "?";
      sawQ = true;
      break;
    default:
      if (thisChar < 32 || thisChar > 126){
        // Convert to octal.
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
  // end the last line, plus length variable
  tmp << "\";" << std::endl
      << "uint32_t " << argv[2] << (splitLen ? "_suffix" : "") << "_len = " << i + total << ";"
      << std::endl;
  tmp.close();
  return 0;
}
