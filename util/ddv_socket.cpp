#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

bool socketError = false;

int DDV_Listen(int port){
  int s = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);//port 8888
  inet_pton(AF_INET, "0.0.0.0", &addr.sin_addr);//listen on all interfaces
  int ret = bind(s, (sockaddr*)&addr, sizeof(addr));//bind to all interfaces, chosen port
  if (ret == 0){
    ret = listen(s, 100);//start listening, backlog of 100 allowed
    if (ret == 0){
      return s;
    }else{
      printf("Listen failed! Error: %s\n", strerror(errno));
      close(s);
      return 0;
    }
  }else{
    printf("Binding failed! Error: %s\n", strerror(errno));
    close(s);
    return 0;
  }
}

int DDV_Accept(int sock){
  return accept(sock, 0, 0);
}

bool DDV_write(void * buffer, int width, int count, int sock){
  bool r = (send(sock, buffer, width*count, 0) == width*count);
  if (!r){
    socketError = true;
    printf("Could not write! %s\n", strerror(errno));
  }
  return r;
}

bool DDV_read(void * buffer, int width, int count, int sock){
  bool r = (recv(sock, buffer, width*count, 0) == width*count);
  if (!r){
    socketError = true;
    printf("Could not read! %s\n", strerror(errno));
  }
  return r;
}
