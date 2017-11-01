#include "../lib/downloader.cpp"
#include <iostream>

int main(int argc, char ** argv){
  if (argc < 2){
    std::cout << "Usage: " << argv[0] << " URL" << std::endl;
    return 1;
  }
  HTTP::Downloader d;
  if (d.get(argv[1])){
    std::cout << d.data() << std::endl;
    std::cerr << "Download success!" << std::endl;
  }else{
    std::cerr << "Download fail!" << std::endl;
  }
  return 0;
}

