#pragma once
#include "http_parser.h"
#include "socket.h"
#include "util.h"

namespace HTTP{
  class Websocket{
  public:
    Websocket(Socket::Connection &c, HTTP::Parser &h);
    operator bool() const;
    bool readFrame();
    bool readLoop();
    void sendFrame(const char * data, unsigned int len, unsigned int frameType = 1);
    void sendFrame(const std::string & data);
    Util::ResizeablePointer data;
    uint8_t frameType;
  private:
    Socket::Connection &C;
    HTTP::Parser &H;
  };
}// namespace HTTP

