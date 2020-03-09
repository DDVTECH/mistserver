#include "../lib/urireader.cpp"
#include <iostream>

class URITest : public Util::DataCallback{
public:
  int goStdin(bool useCallback = true);
  int goHTTP(char *, bool useCallback = true);
  void dump(const char *ptr, size_t size);
  size_t wanted = 100000000;
  void dataCallback(const char *ptr, size_t size);
};

void URITest::dataCallback(const char *ptr, size_t size){
  dump(ptr, size);
}

void URITest::dump(const char *ptr, size_t size){
  if (fwrite(ptr, sizeof(char), size, stdout) != size){INFO_MSG("error: %s", strerror(errno));}
}

int URITest::goStdin(bool useCallback){
  HTTP::URIReader U;
  HTTP::URL uri;
  uri.path = "-";
  U.open(uri);

  if (useCallback){
    MEDIUM_MSG("read from STDIN with callbacks");
    U.readAll(*this);
  }else{
    MEDIUM_MSG("read from STDIN without callbacks");
    char *dPtr = 0;
    size_t dLen = 0;
    U.readAll(dPtr, dLen);
    dump(dPtr, dLen);
    // INFO_MSG("length: %d", dLen);
  }

  return 0;
}

int URITest::goHTTP(char *uri, bool useCallback){
  HTTP::URIReader d;
  d.open(uri);

  if (useCallback){
    MEDIUM_MSG("read file or url with callbacks");
    // d.readAll(*this);

    while (!d.isEOF()){d.readSome(10486, *this);}

  }else{
    MEDIUM_MSG("read file or url without callbacks");
    char *dPtr = 0;
    size_t dLen = 0;
    d.readAll(dPtr, dLen);
    dump(dPtr, dLen);
  }

  return 0;
}

int main(int argc, char **argv){
  //  Util::Config::printDebugLevel = 10;
  Util::Config cfg;
  cfg.activate();

  URITest t;

  if (!isatty(fileno(stdin))){
    if (argv[1]){
      t.goStdin(false);
    }else{
      t.goStdin();
    }
  }else{

    if (argc == 1){
      std::cout << "no arguments applied!" << std::endl;
      std::cout << "usage: " << std::endl;
      std::cout << "STDIN:\t urireader < filename " << std::endl;
      std::cout << "URL:\t urireader http://url " << std::endl;
      std::cout << "FILE:\t urireader path_to_file " << std::endl << std::endl;
      std::cout << "Outputs content to stdout, use ' > outputfile' after the command to write "
                   "contents to disk"
                << std::endl
                << std::endl;
      return 0;
    }else{
      if (argv[2]){
        t.goHTTP(argv[1], false);
      }else{
        t.goHTTP(argv[1]);
      }
    }
  }

  return 0;
}
