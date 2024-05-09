#include <mist/config.h>
#include <mist/defines.h>
#include <mist/socket.h>
#include <mist/util.h>
#include <mist/stream.h>
#include "output/mist_out.cpp"
#include "output/output_rtmp.h"
#include "output/output_hls.h"

int main(int argc, char *argv[]){
  if (argc < 2) {
    INFO_MSG("usage: %s [MistSomething]", argv[0]);
    return 1;
  }
  // Create a new argv array without argv[1]
  int new_argc = argc - 1;
  char** new_argv = new char*[new_argc];
  for (int i = 0, j = 0; i < argc; ++i) {
      if (i != 1) {
          new_argv[j++] = argv[i];
      }
  }
  if (strcmp(argv[1], "MistOutHLS") == 0) {
    return OutputMain<Mist::OutHLS>(new_argc, new_argv);
  }
  else if (strcmp(argv[1], "MistOutRTMP") == 0) {
    return OutputMain<Mist::OutRTMP>(new_argc, new_argv);
  }
  INFO_MSG("binary not found: %s", argv[1]);
  return 0;
}
