#include <fcntl.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "../util/flv.cpp" //FLV format parser
#include "../util/ddv_socket.cpp" //DDV Socket lib

#include <sys/epoll.h>

void termination_handler (int signum){
  return;
}


struct buffer{
  int number;
  bool iskeyframe;
  FLV_Pack * FLV;
  buffer(){
    number = -1;
    iskeyframe = false;
    FLV = 0;
  }//constructor
};//buffer

class user{
  public:
    int MyBuffer;
    int MyBuffer_num;
    int MyBuffer_len;
    int MyNum;
    int currsend;
    void * lastpointer;
    static int UserCount;
    int s;
    user(int fd){
      s = fd;
      MyNum = UserCount++;
      std::cout << "User " << MyNum << " connected" << std::endl;
    }//constructor
    void Disconnect(std::string reason) {
      if (s != -1) {
        close(s);
        s = -1;
        std::cout << "Disconnected user " << MyNum << ": " << reason << std::endl;
      }
    }//Disconnect
    bool doSend(char * buffer, int todo){
      int r = send(s, buffer+currsend, todo-currsend, 0);
      if (r <= 0){
        if ((r < 0) && (errno == EWOULDBLOCK)){return false;}
        Disconnect("Connection closed");
        return false;
      }
      currsend += r;
      return (currsend == todo);
    }
    void Send(buffer ** ringbuf, int buffers){
      //not connected? cancel
      if (s < 0){return;}
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
        Disconnect("Buffer resize at wrong time... had to disconnect");
        return;
      }
      if (doSend(ringbuf[MyBuffer]->FLV->data, MyBuffer_len)){
        //completed a send - switch to next buffer
        if ((ringbuf[MyBuffer]->number != MyBuffer_num)){
          std::cout << "Warning: User " << MyNum << " was send corrupt video data and send to the next keyframe!" << std::endl;
          int nocrashcount = 0;
          do{
            MyBuffer++;
            nocrashcount++;
            MyBuffer %= buffers;
          }while(!ringbuf[MyBuffer]->FLV->isKeyframe && (nocrashcount < buffers));
          if (nocrashcount >= buffers){
            std::cout << "Warning: No keyframe found in buffers! Skipping search for now..." << std::endl;
            return;
          }
        }else{
          MyBuffer++;
          MyBuffer %= buffers;
        }
        MyBuffer_num = -1;
        lastpointer = 0;
        currsend = 0;
      }//completed a send
    }//send
};
int user::UserCount = 0;

int main( int argc, char * argv[] ) {
  struct sigaction new_action;
  new_action.sa_handler = termination_handler;
  sigemptyset (&new_action.sa_mask);
  new_action.sa_flags = 0;
  sigaction (SIGPIPE, &new_action, NULL);
  
  if (argc < 2) {
    std::cout << "usage: " << argv[0] << " buffers_count [streamname]" << std::endl;
    return 1;
  }
  std::string shared_socket = "/tmp/shared_socket";
  if (argc > 2){
    shared_socket = argv[2];
    shared_socket = "/tmp/shared_socket_" + shared_socket;
  }

  int metabuflen = 0;
  char * metabuffer = 0;
  int buffers = atoi(argv[1]);
  buffer ** ringbuf = (buffer**) calloc (buffers,sizeof(buffer*));
  std::vector<user> users;
  std::vector<user>::iterator usersIt;
  for (int i = 0; i < buffers; ++i) ringbuf[i] = new buffer;
  int current_buffer = 0;
  int lastproper = 0;//last properly finished buffer number
  unsigned int loopcount = 0;
  int listener = DDV_UnixListen(shared_socket, true);
  int incoming = 0;

  unsigned char packtype;
  bool gotVideoInfo = false;
  bool gotAudioInfo = false;

  int infile = fileno(stdin);
  int poller = epoll_create(1);
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = infile;
  epoll_ctl(poller, EPOLL_CTL_ADD, infile, &ev);
  struct epoll_event events[1];
  
  
  while(!feof(stdin) && !All_Hell_Broke_Loose){
    //invalidate the current buffer
    ringbuf[current_buffer]->number = -1;
    if ((epoll_wait(poller, events, 1, 10) > 0) && FLV_GetPacket(ringbuf[current_buffer]->FLV)){
      loopcount++;
      packtype = ringbuf[current_buffer]->FLV->data[0];
      //store metadata, if available
      if (packtype == 0x12){
        metabuflen = ringbuf[current_buffer]->FLV->len;
        metabuffer = (char*)realloc(metabuffer, metabuflen);
        memcpy(metabuffer, ringbuf[current_buffer]->FLV->data, metabuflen);
        std::cout << "Received metadata!" << std::endl;
        if (gotVideoInfo && gotAudioInfo){
          All_Hell_Broke_Loose = true;
          std::cout << "... after proper video and audio? Cancelling broadcast!" << std::endl;
        }
        gotVideoInfo = false;
        gotAudioInfo = false;
      }
      if (!gotVideoInfo && ringbuf[current_buffer]->FLV->isKeyframe){
        if ((ringbuf[current_buffer]->FLV->data[11] & 0x0f) == 7){//avc packet
          if (ringbuf[current_buffer]->FLV->data[12] == 0){
            ringbuf[current_buffer]->FLV->data[4] = 0;//timestamp to zero
            ringbuf[current_buffer]->FLV->data[5] = 0;//timestamp to zero
            ringbuf[current_buffer]->FLV->data[6] = 0;//timestamp to zero
            metabuffer = (char*)realloc(metabuffer, metabuflen + ringbuf[current_buffer]->FLV->len);
            memcpy(metabuffer+metabuflen, ringbuf[current_buffer]->FLV->data, ringbuf[current_buffer]->FLV->len);
            metabuflen += ringbuf[current_buffer]->FLV->len;
            gotVideoInfo = true;
            std::cout << "Received video configuration!" << std::endl;
          }
        }else{gotVideoInfo = true;}//non-avc = no config...
      }
      if (!gotAudioInfo && (packtype == 0x08)){
        if (((ringbuf[current_buffer]->FLV->data[11] & 0xf0) >> 4) == 10){//aac packet
          ringbuf[current_buffer]->FLV->data[4] = 0;//timestamp to zero
          ringbuf[current_buffer]->FLV->data[5] = 0;//timestamp to zero
          ringbuf[current_buffer]->FLV->data[6] = 0;//timestamp to zero
          metabuffer = (char*)realloc(metabuffer, metabuflen + ringbuf[current_buffer]->FLV->len);
          memcpy(metabuffer+metabuflen, ringbuf[current_buffer]->FLV->data, ringbuf[current_buffer]->FLV->len);
          metabuflen += ringbuf[current_buffer]->FLV->len;
          gotAudioInfo = true;
          std::cout << "Received audio configuration!" << std::endl;
        }else{gotAudioInfo = true;}//no aac = no config...
      }
      //on keyframe set start point
      if (packtype == 0x09){
        if (((ringbuf[current_buffer]->FLV->data[11] & 0xf0) >> 4) == 1){
          lastproper = current_buffer;
        }
      }
      //keep track of buffers
      ringbuf[current_buffer]->number = loopcount;
      current_buffer++;
      current_buffer %= buffers;
    }
    
    //check for new connections, accept them if there are any
    incoming = DDV_Accept(listener, true);
    if (incoming >= 0){
      users.push_back(incoming);
      //send the FLV header
      users.back().currsend = 0;
      users.back().MyBuffer = lastproper;
      users.back().MyBuffer_num = -1;
      //TODO: Do this more nicely?
      if (!DDV_write(FLVHeader, 13, incoming)){
        users.back().Disconnect("failed to receive the header!");
      }else{
        if (!DDV_write(metabuffer, metabuflen, incoming)){
          users.back().Disconnect("failed to receive metadata!");
        }
      }
    }
    
    //send all connections what they need, if and when they need it
    if (users.size() > 0){
      for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
        if ((*usersIt).s == -1){
          users.erase(usersIt); break;
        }else{
          (*usersIt).Send(ringbuf, buffers);
        }
      }
    }
  }//main loop

  // disconnect listener
  std::cout << "Reached EOF of input" << std::endl;
  close(listener);
  while (users.size() > 0){
    for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
      (*usersIt).Disconnect("Shutting down...");
      if ((*usersIt).s == -1){users.erase(usersIt);break;}
    }
  }
  return 0;
}
