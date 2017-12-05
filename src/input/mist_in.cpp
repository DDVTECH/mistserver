#include INPUTTYPE 
#include <mist/util.h>

int main(int argc, char * argv[]) {
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  mistIn conv(&conf);
  return conv.boot(argc, argv);
}

