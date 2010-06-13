#pragma once
#include "buffer.h"
#include "sockets/SocketW.h"
#include <iostream>

class user{
  public:
    user();
    ~user();
    void set_buffer(buffer * newBuffer);
    int get_number();
    bool complete_send();
    void disconnect();
    void connect(SWUnixSocket * newConnection);
    bool is_connected();
    int send_msg(char * message, int length, SWBaseSocket::SWBaseError * BError);
    int sent;
    buffer * myBuffer;
    SWUnixSocket * myConnection;
  private:
};//user

