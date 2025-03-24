#include <mist/defines.h>
#include <mist/ev.h>
#include <mist/procs.h>
#include <mist/socket.h>
#include <mist/timing.h>
#include <mist/util.h>

#include <unistd.h>

int main(int argc, char **argv) {
  Socket::Connection logOut;
  Util::printDebugLevel = 10;
  Socket::Connection in(-1, 0);
  Event::Loop ev;
  ev.setup();
  ev.addSocket(0, [&in, &logOut, &ev](void *) {
    if (in.spool()) {
      while (in.Received().size()) {
        logOut.SendNow(in.Received().get());
        in.Received().get().clear();
      }
    }
    if (!in) {
      INFO_MSG("Input closed");
      logOut.close();
      ev.remove(0);
    }
  }, 0);

  std::deque<std::string> args;
  for (char **i = argv + 1; *i; ++i) { args.push_back(*i); }
  pid_t pid = Util::startConverted(args, logOut);
  INFO_MSG("PID=%u", pid);
  while (Util::Procs::childRunning(pid)) { ev.await(10000); }
  INFO_MSG("Shutting down");
  return 0;
}
