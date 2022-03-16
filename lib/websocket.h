#pragma once
#include "http_parser.h"
#include "url.h"
#include "socket.h"
#include "util.h"

namespace HTTP{
  class Websocket{
  public:
    Websocket(Socket::Connection &c, const HTTP::Parser &req, HTTP::Parser &resp);
    Websocket(Socket::Connection &c, const HTTP::URL & url, std::map<std::string, std::string> * headers = 0);
    Websocket(Socket::Connection &c, bool client);
    operator bool() const;
    bool readFrame();
    bool readLoop();
    void sendFrame(const char *data, unsigned int len, unsigned int frameType = 1);
    void sendFrameHead(unsigned int len, unsigned int frameType = 1);
    void sendFrameData(const char *data, unsigned int len);
    void sendFrame(const std::string &data);
    Util::ResizeablePointer data;
    uint8_t frameType;

  private:
    char header[14];///< Header used for currently sending frame, if any
    size_t headLen; ///< Length of header used for currently sending frame
    size_t dataCtr; ///< Tracks payload bytes sent since frame start
    bool maskOut;   ///< True if masking is used for output
    Socket::Connection &C;
  };
}// namespace HTTP
