#pragma once
#include "buffer.h"
#include "sockets/SocketW.h"
#include <iostream>

class user{
  public:
    user();
    ~user();
    void disconnect();
    void connect(SWBaseSocket * newConnection);
    void Send(buffer ** ringbuf, int buffers);
    bool is_connected;
    SWUnixSocket * Conn;
    int MyBuffer;
    int MyBuffer_num;
};//user

