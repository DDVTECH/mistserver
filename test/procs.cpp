#include <mist/json.h>
#include <mist/procs.h>
#include <mist/socket.h>
#include <mist/timing.h>

#include <cassert>
#include <iostream>
#include <signal.h>

bool running = true;
void signal_handler(int signum, siginfo_t *sigInfo, void *ignore) {
  running = false;
}

/// Various tests for the process library
int main(int argc, char **argv) {
  size_t argOff = 1;
  if (argc <= argOff) return 1;
  std::string test = argv[argOff++];

  // Sleeps for 1s and then runs the next command
  if (test == "sleep") {
    Util::sleep(1000);
    if (argc <= argOff) { return 1; }
    test = argv[argOff++];
  }

  // Writes a random string to child process argument
  // expects to read it from child process' standard output in response
  if (test == "output_capture") {
    std::string random = Util::getRandomAlphanumeric(50);
    assert(Util::Procs::getOutputOf({argv[0], "echo", random}, 10000) == random);
    return 0;
  }

  // Writes a random string to child process argument
  // expects to read it from child process' standard output in response
  if (test == "output_delay") {
    std::string random = Util::getRandomAlphanumeric(50);
    assert(Util::Procs::getOutputOf({argv[0], "sleep", "echo", random}, 10000) == random);
    return 0;
  }

  // Writes third argument to standard output
  // Used by output_capture and output_delay tests
  if (test == "echo") {
    if (argc <= argOff) { return 1; }
    std::cout << argv[argOff++];
    return 0;
  }

  // Writes a random string to child process standard input
  // expects to read it back from child process' standard output in response
  if (test == "output_loop") {
    int fdin = -1, fdout = -1;
    pid_t pid = Util::Procs::StartPiped({argv[0], "cat"}, &fdin, &fdout, 0);
    assert(pid != 0);
    std::string random = Util::getRandomAlphanumeric(50);

    Socket::Connection writer(fdin, -1);
    writer.SendNow(random);
    writer.close();

    Socket::Connection C(-1, fdout);
    int attempts = 0;
    while (!C.Received().available(50) && ++attempts < 50) {
      C.spool();
      Util::sleep(1);
    }
    assert(C.Received().remove(50) == random);
    return 0;
  }

  // Runs child_linger_2 as a child and checks if the started processes are stopped
  if (test == "child_linger") {
    JSON::Value procs = JSON::fromString(Util::Procs::getOutputOf({argv[0], "child_linger_2"}, 3000));
    assert(procs.size() == 3);
    jsonForEach (procs, it) {
      assert(it->asInt() > 0);
      assert(kill(it->asInt(), 0) != 0);
    }
    return 0;
  }

  // Runs await/await_slow/block, prints them to stdout and exits
  if (test == "child_linger_2") {
    Util::Procs::kill_timeout = 2;
    JSON::Value out;
    out.append(Util::Procs::StartPiped({argv[0], "await"}, 0, 0, 0));
    out.append(Util::Procs::StartPiped({argv[0], "await_slow"}, 0, 0, 0));
    out.append(Util::Procs::StartPiped({argv[0], "block"}, 0, 0, 0));
    std::cout << out << std::endl;
    return 0;
  }

  // Attempts to connect to bad file descriptors
  if (test == "fds") {
    {
      int a = 0, b = 1, c = 2;
      assert(Util::Procs::StartPiped({argv[0], "echo", "expected_success"}, &a, &b, &c));
    }
    {
      int a = 3, b = 4, c = 5;
      assert(!Util::Procs::StartPiped({argv[0], "echo", "expected_fail"}, &a, &b, &c));
    }
    return 0;
  }

  // Writes standard input to standard output
  // Used by output_loop test
  if (test == "cat") {
    char buffer[50];
    while (std::cin.good()) {
      std::cin.read(buffer, 50);
      size_t n = std::cin.gcount();
      std::cout.write(buffer, n);
    }
    return 0;
  }

  // Sleeps until interrupted
  if (test == "await") {
    struct sigaction new_action;
    new_action.sa_sigaction = signal_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = SA_SIGINFO;
    sigaction(SIGINT, &new_action, NULL);
    while (running) { Util::sleep(100); }
    return 0;
  }

  // Sleeps until interrupted, then exits after 1 second
  if (test == "await_slow") {
    struct sigaction new_action;
    new_action.sa_sigaction = signal_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = SA_SIGINFO;
    sigaction(SIGINT, &new_action, NULL);
    while (running) { Util::sleep(100); }
    Util::wait(1000);
    return 0;
  }

  // Sleeps forever
  if (test == "block") {
    struct sigaction new_action;
    new_action.sa_sigaction = signal_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = SA_SIGINFO;
    sigaction(SIGINT, &new_action, NULL);
    while (true) { Util::sleep(100); }
    return 0;
  }

  std::cerr << "Unknown test: " << test << std::endl;
  return 1;
}
