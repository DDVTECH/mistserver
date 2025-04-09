#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/util.h>

#include <cassert>
#include <sys/resource.h>

enum fun { BASIC = 0, BADFD = 1, PRINT = 2, INPUT = 3, SLEEP = 4, FAIL = 5 };

void runTest(const char *const *argv, int fd[3], bool expected) {

  int *fdin = (fd[0] == -1) ? 0 : &fd[0];
  int *fdout = (fd[1] == -1) ? 0 : &fd[1];
  int *fderr = (fd[2] == -1) ? 0 : &fd[2];

  if (fdin) {
    INFO_MSG("fdin is %d", *fdin)
  } else {
    INFO_MSG("fdin is null")
  };
  if (fdout) {
    INFO_MSG("fdout is %d", *fdout)
  } else {
    INFO_MSG("fdout is null")
  };
  if (fderr) {
    INFO_MSG("fderr is %d", *fderr)
  } else {
    INFO_MSG("fderr is null")
  };
  pid_t pid = Util::Procs::StartPiped(argv, fdin, fdout, fderr);
  assert(expected == (bool)pid);
}

void runTestWithOutput(const char *const *argv, std::string expected) {
  std::string out = Util::Procs::getOutputOf(argv, 10000);
  INFO_MSG("output: %s", out.c_str());
  assert(expected == out);
}

/**
 * Tests functionality of the StartPiped function.
 * \return 1 if the arguments are invalid, 0 otherwise.
 * \arg argc Number of arguments.
 * \arg argv Contains the arguments, in order, starting with a boolean that constrains resources if
 * set to true, followed by three integer arguments for fdin, fdout and fderr.
 */
int main(int argc, char **argv) {
  if (argc != 3 && argc != 5) return 1;
  int i = atoi(argv[1]);

  if (argc == 3) {
    switch (i) { // child process
      case BASIC:
      case BADFD: {
        std::string pid = "lsof -p " + JSON::Value(getpid()).asString();
        system(pid.c_str());
        return 0;
      }
      case PRINT: {
        std::cout << "OUTPUT";
        return 0;
      }
      case INPUT: {
        std::cout << argv[2];
        return 0;
      }
      case SLEEP: {
        Util::sleep(5000);
        std::cout << "OUTPUT";
        return 0;
      }
      case FAIL: {
        std::cout << "OUTPUT";
        return 1;
      }
      default: {
        return 1;
      }
    }
  }

  Util::printDebugLevel = 10;
  auto getValOrEmpty = [&](char *in, int *out) { *out = (strcmp(in, "") != 0) ? atoi(in) : -1; };
  const char *argvv[] = {argv[0], "", "", NULL};

  int fd[3];
  getValOrEmpty(argv[2], &fd[0]);
  getValOrEmpty(argv[3], &fd[1]);
  getValOrEmpty(argv[4], &fd[2]);

  switch (i) { // parent process
    case BASIC: {
      runTest(argvv, fd, true);
      break;
    }
    case BADFD: {
      runTest(argvv, fd, false);
      break;
    }
    case PRINT: {
      argvv[1] = "2";
      runTestWithOutput(argvv, "OUTPUT");
      break;
    }
    case INPUT: {
      argvv[1] = "3";
      argvv[2] = "INPUT";
      runTestWithOutput(argvv, "INPUT");
      break;
    }
    case SLEEP: {
      argvv[1] = "4";
      runTestWithOutput(argvv, "OUTPUT");
      break;
    }
    case FAIL: {
      argvv[1] = "5";
      runTestWithOutput(argvv, "OUTPUT");
      break;
    }
    default: {
      ERROR_MSG("Invalid argument for procs test %d", i);
      return 1;
    }
  }

  return 0;
}
