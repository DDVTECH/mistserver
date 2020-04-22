#include "../lib/urireader.cpp"
#include <iostream>

class URITest : public Util::DataCallback{
public:
  void dump(const char *ptr, size_t size){
    if (fwrite(ptr, sizeof(char), size, stdout) != size){INFO_MSG("error: %s", strerror(errno));}
  }
  void dataCallback(const char *ptr, size_t size){
    dump(ptr, size);
  }
  int main(int argc, char **argv);
};

int URITest::main(int argc, char **argv){
  Util::redirectLogsIfNeeded();
  Util::Config cfg(argv[0]);
  JSON::Value option;
  option["arg_num"] = 1;
  option["arg"] = "string";
  option["help"] = "Name of the input URI or - for stdin";
  option["value"].append("-");
  cfg.addOption("input", option);
  option.null();
  option["short"] = "r";
  option["long"] = "readall";
  option["help"] = "Read all data all at once in blocking mode";
  option["value"].append(0);
  cfg.addOption("readall", option);
  if (!cfg.parseArgs(argc, argv)){return 1;}

  cfg.activate();
  HTTP::URIReader R(cfg.getString("input"));

  if (cfg.getBool("readall")){
    char *dPtr = 0;
    size_t dLen = 0;
    R.readAll(dPtr, dLen);
    dump(dPtr, dLen);
  }else{
    while (!R.isEOF() && cfg.is_active){R.readSome(10486, *this);}
  }
  return 0;
}

int main(int argc, char **argv){
  URITest t;
  t.main(argc, argv);
}
