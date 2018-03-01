#include <mist/util.h>
#include <unistd.h>
#include <iostream>

int main(int argc, char ** argv){
  Util::logParser(0, 1, isatty(1));
  return 0;
}

