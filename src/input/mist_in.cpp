#ifndef ONE_BINARY
#include INPUTTYPE
#endif
#include <mist/util.h>
#include <mist/config.h>

template<class T>
int InputMain(int argc, char *argv[]){
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  T conv(&conf);
  return conv.boot(argc, argv);
}

#ifndef ONE_BINARY
int main(int argc, char *argv[]){
  return InputMain<mistIn>(argc, argv);
}
#endif
