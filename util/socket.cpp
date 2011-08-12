/// \file socket.cpp
/// A handy Socket wrapper library.
/// Written by Jaron Vietor in 2010 for DDVTech

#include "socket.h"

/// Create a new base socket. This is a basic constructor for converting any valid socket to a Socket::Connection.
/// \param sockNo Integer representing the socket to convert.
Socket::Connection::Connection(int sockNo){
  sock = sockNo;
  Error = false;
  Blocking = false;
}//Socket::Connection basic constructor

/// Create a new disconnected base socket. This is a basic constructor for placeholder purposes.
/// A socket created like this is always disconnected and should/could be overwritten at some point.
Socket::Connection::Connection(){
  sock = -1;
  Error = false;
  Blocking = false;
}//Socket::Connection basic constructor

/// Close connection. The internal socket is closed and then set to -1.
void Socket::Connection::close(){
  #if DEBUG >= 4
  fprintf(stderr, "Socket closed.\n");
  #endif
  shutdown(sock, SHUT_RDWR);
  ::close(sock);
  sock = -1;
}//Socket::Connection::close

/// Returns internal socket number.
int Socket::Connection::getSocket(){return sock;}

/// Returns a string describing the last error that occured.
/// Simply calls strerror(errno) - not very reliable!
/// \todo Improve getError at some point to be more reliable and only report socket errors.
std::string Socket::Connection::getError(){return strerror(errno);}

/// Create a new Unix Socket. This socket will (try to) connect to the given address right away.
/// \param address String containing the location of the Unix socket to connect to.
/// \param nonblock Whether the socket should be nonblocking. False by default.
Socket::Connection::Connection(std::string address, bool nonblock){
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
}//Socket::Connection Unix Contructor

/// Returns the ready-state for this socket.
/// \returns 1 if data is waiting to be read, -1 if not connected, 0 otherwise.
signed int Socket::Connection::ready(){
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
    #if DEBUG >= 4
    fprintf(stderr, "Socket ready error - socket is closed.\n");
    #endif
    close();
    return -1;
  }
  return r;
}

/// Returns the connected-state for this socket.
/// Note that this function might be slightly behind the real situation.
/// The connection status is updated after every read/write attempt, when errors occur
/// and when the socket is closed manually.
/// \returns True if socket is connected, false otherwise.
bool Socket::Connection::connected(){
  return (sock >= 0);
}

/// Writes data to socket. This function blocks if the socket is blocking and all data cannot be written right away.
/// If the socket is nonblocking and not all data can be written, this function sets internal variable Blocking to true
/// and returns false.
/// \param buffer Location of the buffer to write from.
/// \param len Amount of bytes to write.
/// \returns True if the whole write was succesfull, false otherwise.
bool Socket::Connection::write(const void * buffer, int len){
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
bool Socket::Connection::read(void * buffer, int len){
  int sofar = 0;
  if (sock < 0){return false;}
  while (sofar != len){
    int r = recv(sock, (char*)buffer + sofar, len-sofar, 0);
    if (r < 0){
      switch (errno){
        case EWOULDBLOCK: return 0; break;
        default:
          Error = true;
          #if DEBUG >= 2
          fprintf(stderr, "Could not read data! Error %i: %s\n", r, strerror(errno));
          #endif
          close();
          break;
      }
      return false;
    }else{
      if (r == 0){
        Error = true;
        #if DEBUG >= 2
        fprintf(stderr, "Could not read data! Socket is closed.\n");
        #endif
        close();
        return false;
      }
      sofar += r;
    }
  }
  return true;
}//Socket::Connection::read

/// Read call that is compatible with file access syntax. This function simply calls the other read function.
bool Socket::Connection::read(void * buffer, int width, int count){return read(buffer, width*count);}
/// Write call that is compatible with file access syntax. This function simply calls the other write function.
bool Socket::Connection::write(void * buffer, int width, int count){return write(buffer, width*count);}
/// Write call that is compatible with std::string. This function simply calls the other write function.
bool Socket::Connection::write(const std::string data){return write(data.c_str(), data.size());}

/// Incremental write call. This function tries to write len bytes to the socket from the buffer,
/// returning the amount of bytes it actually wrote.
/// \param buffer Location of the buffer to write from.
/// \param len Amount of bytes to write.
/// \returns The amount of bytes actually written.
int Socket::Connection::iwrite(void * buffer, int len){
  if (sock < 0){return 0;}
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
  if (r == 0){
    #if DEBUG >= 4
    fprintf(stderr, "Could not iwrite data! Socket is closed.\n");
    #endif
    close();
  }
  return r;
}//Socket::Connection::iwrite

/// Incremental read call. This function tries to read len bytes to the buffer from the socket,
/// returning the amount of bytes it actually read.
/// \param buffer Location of the buffer to read to.
/// \param len Amount of bytes to read.
/// \returns The amount of bytes actually read.
int Socket::Connection::iread(void * buffer, int len){
  if (sock < 0){return 0;}
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
  if (r == 0){
    #if DEBUG >= 4
    fprintf(stderr, "Could not iread data! Socket is closed.\n");
    #endif
    close();
  }
  return r;
}//Socket::Connection::iread

/// Read call that is compatible with std::string.
/// Data is read using iread (which is nonblocking if the Socket::Connection itself is),
/// then appended to end of buffer. This functions reads at least one byte before returning.
/// \param buffer std::string to append data to.
/// \return True if new data arrived, false otherwise.
bool Socket::Connection::read(std::string & buffer){
  char cbuffer[5000];
  if (!read(cbuffer, 1)){return false;}
  int num = iread(cbuffer+1, 4999);
  if (num > 0){
    buffer.append(cbuffer, num+1);
  }else{
    buffer.append(cbuffer, 1);
  }
  return true;
}//read

/// Read call that is compatible with std::string.
/// Data is read using iread (which is nonblocking if the Socket::Connection itself is),
/// then appended to end of buffer.
/// \param buffer std::string to append data to.
/// \return True if new data arrived, false otherwise.
bool Socket::Connection::iread(std::string & buffer){
  char cbuffer[5000];
  int num = iread(cbuffer, 5000);
  if (num < 1){return false;}
  buffer.append(cbuffer, num);
  return true;
}//iread

/// Incremental write call that is compatible with std::string.
/// Data is written using iwrite (which is nonblocking if the Socket::Connection itself is),
/// then removed from front of buffer.
/// \param buffer std::string to remove data from.
/// \return True if more data was sent, false otherwise.
bool Socket::Connection::iwrite(std::string & buffer){
  if (buffer.size() < 1){return false;}
  int tmp = iwrite((void*)buffer.c_str(), buffer.size());
  if (tmp < 1){return false;}
  buffer = buffer.substr(tmp);
  return true;
}//iwrite

/// Write call that is compatible with std::string.
/// Data is written using write (which is always blocking),
/// then removed from front of buffer.
/// \param buffer std::string to remove data from.
/// \return True if more data was sent, false otherwise.
bool Socket::Connection::swrite(std::string & buffer){
  if (buffer.size() < 1){return false;}
  bool tmp = write((void*)buffer.c_str(), buffer.size());
  if (tmp){buffer = "";}
  return tmp;
}//write

/// Gets hostname for connection, if available.
std::string Socket::Connection::getHost(){
  return remotehost;
}

/// Create a new base Server. The socket is never connected, and a placeholder for later connections.
Socket::Server::Server(){
  sock = -1;
}//Socket::Server base Constructor

/// Create a new TCP Server. The socket is immediately bound and set to listen.
/// A maximum of 100 connections will be accepted between accept() calls.
/// Any further connections coming in will be dropped.
/// \param port The TCP port to listen on
/// \param hostname (optional) The interface to bind to. The default is 0.0.0.0 (all interfaces).
/// \param nonblock (optional) Whether accept() calls will be nonblocking. Default is false (blocking).
Socket::Server::Server(int port, std::string hostname, bool nonblock){
  sock = socket(AF_INET6, SOCK_STREAM, 0);
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
  struct sockaddr_in6 addr;
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(port);//set port
  if (hostname == "0.0.0.0"){
    addr.sin6_addr = in6addr_any;
  }else{
    inet_pton(AF_INET6, hostname.c_str(), &addr.sin6_addr);//set interface, 0.0.0.0 (default) is all
  }
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
}//Socket::Server TCP Constructor

/// Create a new Unix Server. The socket is immediately bound and set to listen.
/// A maximum of 100 connections will be accepted between accept() calls.
/// Any further connections coming in will be dropped.
/// The address used will first be unlinked - so it succeeds if the Unix socket already existed. Watch out for this behaviour - it will delete any file located at address!
/// \param address The location of the Unix socket to bind to.
/// \param nonblock (optional) Whether accept() calls will be nonblocking. Default is false (blocking).
Socket::Server::Server(std::string address, bool nonblock){
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
}//Socket::Server Unix Constructor

/// Accept any waiting connections. If the Socket::Server is blocking, this function will block until there is an incoming connection.
/// If the Socket::Server is nonblocking, it might return a Socket::Connection that is not connected, so check for this.
/// \param nonblock (optional) Whether the newly connected socket should be nonblocking. Default is false (blocking).
/// \returns A Socket::Connection, which may or may not be connected, depending on settings and circumstances.
Socket::Connection Socket::Server::accept(bool nonblock){
  if (sock < 0){return Socket::Connection(-1);}
  struct sockaddr_in6 addrinfo;
  socklen_t len = sizeof(addrinfo);
  static char addrconv[INET6_ADDRSTRLEN];
  int r = ::accept(sock, (sockaddr*)&addrinfo, &len);
  //set the socket to be nonblocking, if requested.
  //we could do this through accept4 with a flag, but that call is non-standard...
  if ((r >= 0) && nonblock){
    int flags = fcntl(r, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(r, F_SETFL, flags);
  }
  Socket::Connection tmp(r);
  if (r < 0){
    if (errno != EWOULDBLOCK && errno != EAGAIN){
      #if DEBUG >= 1
      fprintf(stderr, "Error during accept - closing server socket.\n");
      #endif
      close();
    }
  }else{
    if (addrinfo.sin6_family == AF_INET6){
      tmp.remotehost = inet_ntop(AF_INET6, &(addrinfo.sin6_addr), addrconv, INET6_ADDRSTRLEN);
      #if DEBUG >= 4
      fprintf(stderr,"IPv6 addr: %s\n", tmp.remotehost.c_str());
      #endif
    }
    if (addrinfo.sin6_family == AF_INET){
      tmp.remotehost = inet_ntop(AF_INET, &(((sockaddr_in*)&addrinfo)->sin_addr), addrconv, INET6_ADDRSTRLEN);
      #if DEBUG >= 4
      fprintf(stderr,"IPv4 addr: %s\n", tmp.remotehost.c_str());
      #endif
    }
    if (addrinfo.sin6_family == AF_UNIX){
      #if DEBUG >= 4
      tmp.remotehost = ((sockaddr_un*)&addrinfo)->sun_path;
      fprintf(stderr,"Unix addr: %s\n", tmp.remotehost.c_str());
      #endif
      tmp.remotehost = "UNIX_SOCKET";
    }
  }
  return tmp;
}

/// Close connection. The internal socket is closed and then set to -1.
void Socket::Server::close(){
  #if DEBUG >= 4
  fprintf(stderr, "ServerSocket closed.\n");
  #endif
  shutdown(sock, SHUT_RDWR);
  ::close(sock);
  sock = -1;
}//Socket::Server::close

/// Returns the connected-state for this socket.
/// Note that this function might be slightly behind the real situation.
/// The connection status is updated after every accept attempt, when errors occur
/// and when the socket is closed manually.
/// \returns True if socket is connected, false otherwise.
bool Socket::Server::connected(){
  return (sock >= 0);
}//Socket::Server::connected

/// Returns internal socket number.
int Socket::Server::getSocket(){return sock;}
