#include <mist/config.h>
#include <mist/defines.h>
#include <mist/socket.h>
#include <mist/util.h>
#include <mist/stream.h>
#include "output/mist_out.cpp"
#include "output/output_rtmp.h"
#include "output/output_hls.h"
#include "output/output_http_internal.h"
#include "input/mist_in.cpp"
#include "input/input_buffer.h"
#include "session.cpp"
#include "controller/controller.cpp"

int main(int argc, char *argv[]){
  if (argc < 2) {
    return ControllerMain(argc, argv);
  }
  // Create a new argv array without argv[1]
  int new_argc = argc - 1;
  char** new_argv = new char*[new_argc];
  for (int i = 0, j = 0; i < argc; ++i) {
      if (i != 1) {
          new_argv[j++] = argv[i];
      }
  }
 if (strcmp(argv[1], "MistController") == 0) {
    return ControllerMain(new_argc, new_argv);
  }
  if (strcmp(argv[1], "MistOutHLS") == 0) {
    return OutputMain<Mist::OutHLS>(new_argc, new_argv);
  }
  else if (strcmp(argv[1], "MistOutHTTP") == 0) {
    return OutputMain<Mist::OutHTTP>(new_argc, new_argv);
  }
  else if (strcmp(argv[1], "MistOutRTMP") == 0) {
    return OutputMain<Mist::OutRTMP>(new_argc, new_argv);
  }
  else if (strcmp(argv[1], "MistInBuffer") == 0) {
    return InputMain<Mist::inputBuffer>(new_argc, new_argv);
  }
  else if (strcmp(argv[1], "MistSession") == 0) {
    return SessionMain(new_argc, new_argv);
  }
  else {
    return ControllerMain(argc, argv);
  }
  INFO_MSG("binary not found: %s", argv[1]);
  return 202;
}
