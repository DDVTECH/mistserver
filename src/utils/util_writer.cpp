#include <mist/config.h>
#include <mist/ev.h>
#include <mist/http_parser.h>
#include <mist/socket.h>
#include <mist/util.h>

int main(int argc, char **argv) {
  int ret = 2;
  Util::redirectLogsIfNeeded();
  Util::Config config(argv[0]);
  config.addOption("url", R"({"arg":"string", "arg_num":1, "help":"Target URL to pipe standard input towards"})");
  config.parseArgs(argc, argv);

  std::string url = config.getString("url");
  Socket::Connection C;
  if (!Util::externalWriter(url, C, false)) {
    FAIL_MSG("Could not open %s for writing", argv[1]);
    return 1;
  }

  Socket::Connection IO(1, 0);

  Event::Loop ev;
  ev.setup();

  config.activate();

  ev.addSocket(0, [&IO, &C](void *) {
    while (IO.spool()) {
      while (IO.Received().size()) {
        std::string & data = IO.Received().get();
        DONTEVEN_MSG("Send: %s", data.c_str());
        C.SendNow(data);
        data.clear();
      }
    }
  }, 0);

  HTTP::Parser response;
  if (Socket::checkTrueSocket(C.getSocket())) {
    ev.addSocket(C.getSocket(), [&C, &response, &ret](void *) {
      while (C.spool()) {
        if (response.Read(C)) {
          INFO_MSG("Server response: %s %s", response.url.c_str(), response.method.c_str());
          // If the response is a 2XX code, return 0, otherwise return the default response (2).
          if (response.url.size() && response.url[0] == '2') { ret = 0; }
          // Close the connection to shut down the application
          C.close();
        }
      }
    }, 0);
  }

  bool ended = false;
  while ((IO || C) && config.is_active) {
    ev.await(10000);
    if (!IO && !ended) {
      ev.remove(0);
      ev.remove(1);
      INFO_MSG("Completed write");
      C.SendNow(0, 0); // Send ending chunk if needed
      ended = true;
    }
  }
  IO.close();
  C.close();

  return ret;
}
