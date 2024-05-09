#include <mist/util.h>

template<class T>
int InputMain(int argc, char *argv[]){
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  T conv(&conf);
  return conv.boot(argc, argv);
}
