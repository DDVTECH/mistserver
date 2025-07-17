#include <mist/downloader.h>
#include <mist/ev.h>

#include <condition_variable>
#include <iostream>
#include <thread>

std::condition_variable cv;
std::mutex mut;
int boundPort = 0;
std::string bodyData;
Socket::Server srv;

/// Thread that serves a single request from a random port and gives it the data in bodyData as response.
void serving() {
  srv = Socket::Server(0, "127.0.0.1", false);
  {
    std::lock_guard<std::mutex> g(mut);
    boundPort = srv.getBoundAddr().port();
    if (!boundPort) { boundPort = -1; }
  }
  std::cerr << "Bound to port " << boundPort << std::endl;
  cv.notify_all();
  if (boundPort == -1) { return; }
  Socket::Connection C = srv.accept();
  srv.close();
  HTTP::Parser H;
  while (C) {
    if (C.spool()) {
      if (H.Read(C)) {
        H.body = bodyData;
        H.SendResponse("200", "OK", C);
        C.close();
      }
    }
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Usage: " << argv[0] << " URL" << std::endl;
    std::cout << "  URL may be 'AUTO' to use a second thread listening for a single connection on a random port" << std::endl;
    std::cout << "Also takes the following environment variables to change behaviour:" << std::endl;
    std::cout << "  EVENTLOOP: Tests using the event loop system" << std::endl;
    std::cout << "  DATA: Payload to compare with (for AUTO mode; randomly generated if not given)" << std::endl;
    return 1;
  }
  char *data = getenv("DATA");
  bool eventLoop = getenv("EVENTLOOP");
  if (data) { bodyData = data; }

  std::thread serveThread;
  std::string urlStr = argv[1];
  bool selfServe = (urlStr == "AUTO");
  if (selfServe) {
    if (!bodyData.size()) { bodyData = Util::getRandomAlphanumeric(1024 * 1024); }
    serveThread = std::thread(serving);
    {
      std::unique_lock<std::mutex> g(mut);
      cv.wait(g, []() { return boundPort != 0; });
    }
    if (boundPort == -1) {
      std::cerr << "Could not listen on local port!" << std::endl;
      return 1;
    }
    urlStr = "http://127.0.0.1:" + std::to_string(boundPort);
  }
  HTTP::URL url(urlStr);
  std::cerr << "Downloading " << url.getUrl() << std::endl;
  HTTP::Downloader d;
  int ret = -1;
  bool getSuccess = false;
  std::string recvStr;

  // By default, print data to stdout
  size_t dataSoFar = 0;
  std::function<size_t()> resumePos = [&]() { return dataSoFar; };
  std::function<void(const char *, size_t)> onData = [&](const char *ptr, size_t len) {
    std::cout.write(ptr, len);
    dataSoFar += len;
  };

  // If we have a verifier, don't print to stdout but instead store in memory
  if (bodyData.size()) {
    resumePos = [&]() { return recvStr.size(); };
    onData = [&](const char *ptr, size_t len) { recvStr.append(ptr, len); };
  }

  auto checkResult = [&]() {
    if (getSuccess) {
      if (d.isOk()) {
        if (bodyData.size()) {
          if (recvStr == bodyData) {
            std::cerr << "Download success (" << recvStr.size() << "b); data match!" << std::endl;
            ret = 0;
          } else {
            std::cerr << "Downloaded data (" << recvStr.size() << "b) does not match expected (" << bodyData.size()
                      << "b)" << std::endl;
            ret = 2;
          }
        } else {
          std::cerr << "Download success; " << dataSoFar << "b written to stdout" << std::endl;
          ret = 0;
        }
      } else {
        std::cerr << "Download failed: " << d.getStatusCode() << " - " << d.getStatusText() << std::endl;
        ret = 1;
      }
    } else {
      std::cerr << "Download failed!" << std::endl;
      ret = 1;
    }
  };

  if (eventLoop) {
    Event::Loop evLp;
    evLp.setup();

    // Set up the event-looped request
    d.getEventLooped(evLp, url, 10, [&]() {
      getSuccess = true;
      checkResult();
    }, [&]() {
      getSuccess = false;
      checkResult();
    }, resumePos, onData);

    while (ret == -1) { evLp.await(1000); }
  } else {
    getSuccess = d.get(url, resumePos, onData);
    checkResult();
  }
  if (selfServe && serveThread.joinable()) {
    srv.close();
    serveThread.join();
  }
  return ret;
}
