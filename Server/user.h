#include "buffer.h"
#include "sockets/SocketW.h"

class user{
    user();
    ~user();
    void set_buffer(buffer * newBuffer);
    int get_number();
    bool complete_send();
    void disconnect();
    void connect(SWBaseSocket * newConnection);
  private:
    int sent;
    buffer * myBuffer;
    SWBaseSocket * myConnection;
};//user

user::user() { }

user::~user() {
  myConnection->disconnect();
  myConnection = NULL;
}

void user::set_buffer(buffer * newBuffer) {
  myBuffer = newBuffer;
  sent = 0;
}

int user::get_number() {
  return myBuffer->number;
}

bool user::complete_send() {
  if (sent == myBuffer->size) { return true; }
  return false;
}

void user::disconnect() {
  if (myConnection) {
    myConnection->disconnect();
    myConnection = NULL;
  }
}

void user::connect(SWBaseSocket * newConnection) {
  myConnection = newConnection;
}

bool user::is_connected( ) {
  if (myConnection) { return true; }
  return false;
}
