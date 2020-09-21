#include "../lib/downloader.cpp"
#include <iostream>


class CB : public Util::DataCallback {
  virtual void dataCallback(const char *ptr, size_t size){
    std::cout.write(ptr, size);
  }
};
CB callback;

int main(int argc, char **argv){
  if (argc < 2){
    std::cout << "Usage: " << argv[0] << " URL" << std::endl;
    return 1;
  }
  HTTP::Downloader d;
  HTTP::URL url(argv[1]);
  if (d.get(url, 10, callback)){
    std::cerr << "Download success!" << std::endl;
  }else{
    std::cerr << "Download fail!" << std::endl;
  }
  return 0;
}
