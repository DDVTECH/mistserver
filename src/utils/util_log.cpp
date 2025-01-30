#include <mist/procs.h>
#include <mist/util.h>

int main(int argc, char **argv) {
  if (argc == 1) {
    Util::logParser(0, 1, isatty(1));
    return 0;
  }

  int fdIn = STDIN_FILENO, fdOut = -1, fdErr = -1;
  int pid = Util::Procs::StartPiped(argv + 1, &fdIn, &fdOut, &fdErr);
  if (!pid) {
    FAIL_MSG("Failed to spawn child process!");
    return 1;
  }

  Util::logConverter(fdErr, fdOut, STDERR_FILENO, argv[1], pid);
  return 0;
}
