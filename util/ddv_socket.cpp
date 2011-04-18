/// \file ddv_socket.cpp
/// Holds all code for the DDV namespace.

#include "ddv_socket.h"

/// Create a new base socket. This is a basic constructor for converting any valid socket to a DDV::Socket.
/// \param sockNo Integer representing the socket to convert.
DDV::Socket::Socket(int sockNo){
  sock = sockNo;
  Error = false;
  Blocking = false;
}//DDV::Socket basic constructor

/// Create a new disconnected base socket. This is a basic constructor for placeholder purposes.
/// A socket created like this is always disconnected and should/could be overwritten at some point.
DDV::Socket::Socket(){
  sock = -1;
  Error = false;
  Blocking = false;
}//DDV::Socket basic constructor

/// Close connection. The internal socket is closed and then set to -1.
void DDV::Socket::close(){
  #if DEBUG >= 3
  fprintf(stderr, "Socket closed.\n");
  #endif
  ::close(sock);
  sock = -1;
}//DDV::Socket::close

/// Returns internal socket number.
int DDV::Socket::getSocket(){return sock;}

/// Create a new Unix Socket. This socket will (try to) connect to the given address right away.
/// \param address String containing the location of the Unix socket to connect to.
/// \param nonblock Whether the socket should be nonblocking. False by default.
DDV::Socket::Socket(std::string address, bool nonblock){
  sock = socket(PF_UNIX, SOCK_STREAM, 0);
  if (sock < 0){
    #if DEBUG >= 1
    fprintf(stderr, "Could not create socket! Error: %s\n", strerror(errno));
    #endif
    return;
  }
  Error = false;
  Blocking = false;
  sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, address.c_str(), address.size()+1);
  int r = connect(sock, (sockaddr*)&addr, sizeof(addr));
  if (r == 0){
    if (nonblock){
      int flags = fcntl(sock, F_GETFL, 0);
      flags |= O_NONBLOCK;
      fcntl(sock, F_SETFL, flags);
    }
  }else{
    #if DEBUG >= 1
    fprintf(stderr, "Could not connect to %s! Error: %s\n", address.c_str(), strerror(errno));
    #endif
    close();
  }
}//DDV::Socket Unix Contructor

/// Returns the ready-state for this socket.
/// \returns 1 if data is waiting to be read, -1 if not connected, 0 otherwise.
signed int DDV::Socket::ready(){
  if (sock < 0) return -1;
  char tmp;
  int preflags = fcntl(sock, F_GETFL, 0);
  int postflags = preflags | O_NONBLOCK;
  fcntl(sock, F_SETFL, postflags);
  int r = recv(sock, &tmp, 1, MSG_PEEK);
  fcntl(sock, F_SETFL, preflags);
  if (r < 0){
    if (errno == EAGAIN || errno == EWOULDBLOCK){
      return 0;
    }else{
      #if DEBUG >= 2
      fprintf(stderr, "Socket ready error! Error: %s\n", strerror(errno));
      #endif
      close();
      return -1;
    }
  }
  if (r == 0){
    close(); return -1;
  }
  return r;
}

/// Returns the connected-state for this socket.
/// Note that this function might be slightly behind the real situation.
/// The connection status is updated after every read/write attempt, when errors occur
/// and when the socket is closed manually.
/// \returns True if socket is connected, false otherwise.
bool DDV::Socket::connected(){
  return (sock >= 0);
}

/// Writes data to socket. This function blocks if the socket is blocking and all data cannot be written right away.
/// If the socket is nonblocking and not all data can be written, this function sets internal variable Blocking to true
/// and returns false.
/// \param buffer Location of the buffer to write from.
/// \param len Amount of bytes to write.
/// \returns True if the whole write was succesfull, false otherwise.
bool DDV::Socket::write(const void * buffer, int len){
  int sofar = 0;
  if (sock < 0){return false;}
  while (sofar != len){
    int r = send(sock, (char*)buffer + sofar, len-sofar, 0);
    if (r <= 0){
      Error = true;
      #if DEBUG >= 2
      fprintf(stderr, "Could not write data! Error: %s\n", strerror(errno));
      #endif
      close();
      return false;
    }else{
      sofar += r;
    }
  }
  return true;
}//DDv::Socket::write


/// Reads data from socket. This function blocks if the socket is blocking and all data cannot be read right away.
/// If the socket is nonblocking and not all data can be read, this function sets internal variable Blocking to true
/// and returns false.
/// \param buffer Location of the buffer to read to.
/// \param len Amount of bytes to read.
/// \returns True if the whole read was succesfull, false otherwise.
bool DDV::Socket::read(void * buffer, int len){
  int sofar = 0;
  if (sock < 0){return false;}
  while (sofar != len){
    int r = recv(sock, (char*)buffer + sofar, len-sofar, 0);
    if (r <= 0){
      Error = true;
      #if DEBUG >= 2
      fprintf(stderr, "Could not read data! Error: %s\n", strerror(errno));
      #endif
      close();
      return false;
    }else{
      sofar += r;
    }
  }
  return true;
}//DDV::Socket::read

/// Read call that is compatible with file access syntax. This function simply calls the other read function.
bool DDV::Socket::read(void * buffer, int width, int count){return read(buffer, width*count);}
/// Write call that is compatible with file access syntax. This function simply calls the other write function.
bool DDV::Socket::write(void * buffer, int width, int count){return write(buffer, width*count);}
/// Write call that is compatible with std::string. This function simply calls the other write function.
bool DDV::Socket::write(const std::string data){return write(data.c_str(), data.size());}

/// Incremental write call. This function tries to write len bytes to the socket from the buffer,
/// returning the amount of bytes it actually wrote.
/// \param buffer Location of the buffer to write from.
/// \param len Amount of bytes to write.
/// \returns The amount of bytes actually written.
int DDV::Socket::iwrite(void * buffer, int len){
  int r = send(sock, buffer, len, 0);
  if (r < 0){
    switch (errno){
      case EWOULDBLOCK: return 0; break;
      default:
        Error = true;
        #if DEBUG >= 2
        fprintf(stderr, "Could not iwrite data! Error: %s\n", strerror(errno));
        #endif
        close();
        return 0;
        break;
    }
  }
  return r;
}//DDV::Socket::iwrite

/// Incremental read call. This function tries to read len bytes to the buffer from the socket,
/// returning the amount of bytes it actually read.
/// \param buffer Location of the buffer to read to.
/// \param len Amount of bytes to read.
/// \returns The amount of bytes actually read.
int DDV::Socket::iread(void * buffer, int len){
  int r = recv(sock, buffer, len, 0);
  if (r < 0){
    switch (errno){
      case EWOULDBLOCK: return 0; break;
      default:
        Error = true;
        #if DEBUG >= 2
        fprintf(stderr, "Could not iread data! Error: %s\n", strerror(errno));
        #endif
        close();
        return 0;
        break;
    }
  }
  return r;
}//DDV::Socket::iread

/// Read call that is compatible with std::string.
/// Data is read using iread (which is nonblocking if the DDV::Socket itself is),
/// then appended to end of buffer.
/// \param buffer std::string to append data to.
/// \return True if new data arrived, false otherwise.
bool DDV::Socket::read(std::string & buffer){
  char cbuffer[5000];
  int num = iread(cbuffer, 5000);
  if (num > 0){
    buffer.append(cbuffer, num);
    return true;
  }
  return false;
}//read

/// Create a new base ServerSocket. The socket is never connected, and a placeholder for later connections.
DDV::ServerSocket::ServerSocket(){
  sock = -1;
}//DDV::ServerSocket base Constructor
  
/// Create a new TCP ServerSocket. The socket is immediately bound and set to listen.
/// A maximum of 100 connections will be accepted between accept() calls.
/// Any further connections coming in will be dropped.
/// \param port The TCP port to listen on
/// \param hostname (optional) The interface to bind to. The default is 0.0.0.0 (all interfaces).
/// \param nonblock (optional) Whether accept() calls will be nonblocking. Default is false (blocking).
DDV::ServerSocket::ServerSocket(int port, std::string hostname, bool nonblock){
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0){
    #if DEBUG >= 1
    fprintf(stderr, "Could not create socket! Error: %s\n", strerror(errno));
    #endif
    return;
  }
  int on = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  if (nonblock){
    int flags = fcntl(sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);
  }
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);//set port
  inet_pton(AF_INET, hostname.c_str(), &addr.sin_addr);//set interface, 0.0.0.0 (default) is all
  int ret = bind(sock, (sockaddr*)&addr, sizeof(addr));//do the actual bind
  if (ret == 0){
    ret = listen(sock, 100);//start listening, backlog of 100 allowed
    if (ret == 0){
      return;
    }else{
      #if DEBUG >= 1
      fprintf(stderr, "Listen failed! Error: %s\n", strerror(errno));
      #endif
      close();
      return;
    }
  }else{
    #if DEBUG >= 1
    fprintf(stderr, "Binding failed! Error: %s\n", strerror(errno));
    #endif
    close();
    return;
  }
}//DDV::ServerSocket TCP Constructor

/// Create a new Unix ServerSocket. The socket is immediately bound and set to listen.
/// A maximum of 100 connections will be accepted between accept() calls.
/// Any further connections coming in will be dropped.
/// The address used will first be unlinked - so it succeeds if the Unix socket already existed. Watch out for this behaviour - it will delete any file located at address!
/// \param address The location of the Unix socket to bind to.
/// \param nonblock (optional) Whether accept() calls will be nonblocking. Default is false (blocking).
DDV::ServerSocket::ServerSocket(std::string address, bool nonblock){
  unlink(address.c_str());
  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0){
    #if DEBUG >= 1
    fprintf(stderr, "Could not create socket! Error: %s\n", strerror(errno));
    #endif
    return;
  }
  if (nonblock){
    int flags = fcntl(sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);
  }
  sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, address.c_str(), address.size()+1);
  int ret = bind(sock, (sockaddr*)&addr, sizeof(addr));
  if (ret == 0){
    ret = listen(sock, 100);//start listening, backlog of 100 allowed
    if (ret == 0){
      return;
    }else{
      #if DEBUG >= 1
      fprintf(stderr, "Listen failed! Error: %s\n", strerror(errno));
      #endif
      close();
      return;
    }
  }else{
    #if DEBUG >= 1
    fprintf(stderr, "Binding failed! Error: %s\n", strerror(errno));
    #endif
    close();
    return;
  }
}//DDV::ServerSocket Unix Constructor

/// Accept any waiting connections. If the DDV::ServerSocket is blocking, this function will block until there is an incoming connection.
/// If the DDV::ServerSocket is nonblocking, it might return a DDV::Socket that is not connected, so check for this.
/// \param nonblock (optional) Whether the newly connected socket should be nonblocking. Default is false (blocking).
/// \returns A DDV::Socket, which may or may not be connected, depending on settings and circumstances.
DDV::Socket DDV::ServerSocket::accept(bool nonblock){
  if (sock < 0){return DDV::Socket(-1);}
  int r = ::accept(sock, 0, 0);
  //set the socket to be nonblocking, if requested.
  //we could do this through accept4 with a flag, but that call is non-standard...
  if ((r >= 0) && nonblock){
    int flags = fcntl(r, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(r, F_SETFL, flags);
  }
  if (r < 0){
    if (errno != EWOULDBLOCK && errno != EAGAIN){close();}
  }
  return DDV::Socket(r);
}

/// Close connection. The internal socket is closed and then set to -1.
void DDV::ServerSocket::close(){
  ::close(sock);
  sock = -1;
}//DDV::ServerSocket::close

/// Returns the connected-state for this socket.
/// Note that this function might be slightly behind the real situation.
/// The connection status is updated after every accept attempt, when errors occur
/// and when the socket is closed manually.
/// \returns True if socket is connected, false otherwise.
bool DDV::ServerSocket::connected(){
  return (sock >= 0);
}//DDV::ServerSocket::connected

/// Returns internal socket number.
int DDV::ServerSocket::getSocket(){return sock;}
