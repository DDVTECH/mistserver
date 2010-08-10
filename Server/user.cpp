#include "buffer.h"
#include "../sockets/SocketW.h"
#include <iostream>

class user{
  public:
    user(SWBaseSocket * newConn);
    ~user();
    void disconnect(std::string reason);
    void Send(buffer ** ringbuf, int buffers);
    bool is_connected;
    SWUnixSocket * Conn;
    int MyBuffer;
    int MyBuffer_num;
};//user


user::user(SWBaseSocket * newConn) {
  Conn = (SWUnixSocket*)newConn;
  is_connected = (Conn != 0);
}

user::~user(){disconnect("Destroying object");}

void user::disconnect(std::string reason) {
  if (Conn) {
    Conn->disconnect();
    Conn = NULL;
    std::cout << "Disconnected user: " << reason << std::endl;
  }
  is_connected = false;
}

void user::Send(buffer ** ringbuf, int buffers){
  //not connected? cancel
  if (!is_connected){return;}
  //still waiting for next buffer? check it
  if (MyBuffer_num < 0){MyBuffer_num = ringbuf[MyBuffer]->number;}
  //still waiting? don't crash - wait longer.
  if (MyBuffer_num < 0){return;}
  //buffer number changed? disconnect
  if ((ringbuf[MyBuffer]->number != MyBuffer_num)){
    disconnect("Buffer number changed (connection too slow)");
    return;
  }
  SWBaseSocket::SWBaseError err;
  int ret = Conn->fsend(ringbuf[MyBuffer]->FLV->data, ringbuf[MyBuffer]->FLV->len, &err);
  if ((err != SWBaseSocket::ok) && (err != SWBaseSocket::notReady)){
    disconnect("Socket error");
    return;
  }
  if (ret == ringbuf[MyBuffer]->FLV->len){
    //completed a send - switch to next buffer
    MyBuffer++;
    MyBuffer %= buffers;
    MyBuffer_num = -1;
  }
}