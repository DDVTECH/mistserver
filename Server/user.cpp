#include "user.h"

user::user() {
  myBuffer = NULL;
  myConnection = NULL;
}

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

void user::connect(SWUnixSocket * newConnection) {
  myConnection = newConnection;
}

bool user::is_connected( ) {
std::cout << "  -  Checking...:";
  if (myConnection) { std::cout << " true\n"; return true; }
  std::cout << " false\n";
  return false;
}

int user::send_msg(char * message, int length, SWBaseSocket::SWBaseError * BError) {
  return myConnection->send(message,length,BError);
}
