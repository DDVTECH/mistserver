#include "buffer.h"
#include "../sockets/SocketW.h"
#include <iostream>

class user{
  public:
    user(SWBaseSocket * newConn);
    void disconnect(std::string reason);
    void Send(buffer ** ringbuf, int buffers);
    bool is_connected;
    SWUnixSocket * Conn;
    int MyBuffer;
    int MyBuffer_num;
    int MyBuffer_len;
    int MyNum;
    void * lastpointer;
    static int UserCount;
    static SWBaseSocket::SWBaseError err;
};//user

int user::UserCount = 0;
SWBaseSocket::SWBaseError user::err;

user::user(SWBaseSocket * newConn) {
  Conn = (SWUnixSocket*)newConn;
  is_connected = (Conn != 0);
  MyNum = UserCount++;
  std::cout << "User " << MyNum << " connected" << std::endl;
}

void user::disconnect(std::string reason) {
  if (Conn) {
    Conn->disconnect(&err);
    Conn = NULL;
    std::cout << "Disconnected user " << MyNum << ": " << reason << std::endl;
  }
  is_connected = false;
}

void user::Send(buffer ** ringbuf, int buffers){
  //not connected? cancel
  if (!is_connected){return;}
  //still waiting for next buffer? check it
  if (MyBuffer_num < 0){
    MyBuffer_num = ringbuf[MyBuffer]->number;
    //still waiting? don't crash - wait longer.
    if (MyBuffer_num < 0){
      return;
    }else{
      MyBuffer_len = ringbuf[MyBuffer]->FLV->len;
      lastpointer = ringbuf[MyBuffer]->FLV->data;
    }
  }
  if (lastpointer != ringbuf[MyBuffer]->FLV->data){
    disconnect("Buffer resize at wrong time... had to disconnect");
    return;
  }
  int ret = Conn->fsend(ringbuf[MyBuffer]->FLV->data, MyBuffer_len, &err);
  if ((err != SWBaseSocket::ok) && (err != SWBaseSocket::notReady)){
    disconnect("Socket error: " + err.get_error());
    return;
  }
  if (ret == MyBuffer_len){
    //completed a send - switch to next buffer
    if ((ringbuf[MyBuffer]->number != MyBuffer_num)){
      std::cout << "Warning: User " << MyNum << " was send corrupt video data and send to the next keyframe!" << std::endl;
      do{
        MyBuffer++;
        MyBuffer %= buffers;
      }while(!ringbuf[MyBuffer]->FLV->isKeyframe);
    }else{
      MyBuffer++;
      MyBuffer %= buffers;
    }
    MyBuffer_num = -1;
    lastpointer = 0;
  }
}
