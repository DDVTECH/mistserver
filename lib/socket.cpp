/// \file socket.cpp
/// A handy Socket wrapper library.
/// Written by Jaron Vietor in 2010 for DDVTech

#include "socket.h"
#include <sys/stat.h>
#include <poll.h>
#include <netdb.h>
#include <sstream>

#ifdef __FreeBSD__
#include <netinet/in.h>
#endif

std::string uint2string(unsigned int i){
  std::stringstream st;
  st << i;
  return st.str();
}

/// Create a new base socket. This is a basic constructor for converting any valid socket to a Socket::Connection.
/// \param sockNo Integer representing the socket to convert.
Socket::Connection::Connection(int sockNo){
  sock = sockNo;
  pipes[0] = -1;
  pipes[1] = -1;
  up = 0;
  down = 0;
  conntime = time(0);
  Error = false;
  Blocking = false;
}//Socket::Connection basic constructor

/// Simulate a socket using two file descriptors.
/// \param write The filedescriptor to write to.
/// \param read The filedescriptor to read from.
Socket::Connection::Connection(int write, int read){
  sock = -1;
  pipes[0] = write;
  pipes[1] = read;
  up = 0;
  down = 0;
  conntime = time(0);
  Error = false;
  Blocking = false;
}//Socket::Connection basic constructor

/// Create a new disconnected base socket. This is a basic constructor for placeholder purposes.
/// A socket created like this is always disconnected and should/could be overwritten at some point.
Socket::Connection::Connection(){
  sock = -1;
  pipes[0] = -1;
  pipes[1] = -1;
  up = 0;
  down = 0;
  conntime = time(0);
  Error = false;
  Blocking = false;
}//Socket::Connection basic constructor


/// Set this socket to be blocking (true) or nonblocking (false).
void Socket::Connection::setBlocking(bool blocking){
  int flags = fcntl(sock, F_GETFL, 0);
  if (!blocking){
    flags |= O_NONBLOCK;
  }else{
    flags &= !O_NONBLOCK;
  }
  fcntl(sock, F_SETFL, flags);
}

/// Close connection. The internal socket is closed and then set to -1.
/// If the connection is already closed, nothing happens.
void Socket::Connection::close(){
  if (connected()){
    #if DEBUG >= 6
    fprintf(stderr, "Socket closed.\n");
    #endif
    if (sock != -1){
      shutdown(sock, SHUT_RDWR);
      ::close(sock);
      sock = -1;
    }
    if (pipes[0] != -1){
      ::close(pipes[0]);
      pipes[0] = -1;
    }
    if (pipes[1] != -1){
      ::close(pipes[1]);
      pipes[1] = -1;
    }
  }
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
  up = 0;
  down = 0;
  conntime = time(0);
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

/// Create a new TCP Socket. This socket will (try to) connect to the given host/port right away.
/// \param host String containing the hostname to connect to.
/// \param port String containing the port to connect to.
/// \param nonblock Whether the socket should be nonblocking.
Socket::Connection::Connection(std::string host, int port, bool nonblock){
  struct addrinfo *result, *rp, hints;
  Error = false;
  Blocking = false;
  up = 0;
  down = 0;
  conntime = time(0);
  std::stringstream ss;
  ss << port;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_ADDRCONFIG;
  hints.ai_protocol = 0;
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;
  int s = getaddrinfo(host.c_str(), ss.str().c_str(), &hints, &result);
  if (s != 0){
    #if DEBUG >= 1
    fprintf(stderr, "Could not connect to %s:%i! Error: %s\n", host.c_str(), port, gai_strerror(s));
    #endif
    close();
    return;
  }

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock < 0){continue;}
    if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0){break;}
    ::close(sock);
  }
  freeaddrinfo(result);

  if (rp == 0){
    #if DEBUG >= 1
    fprintf(stderr, "Could not connect to %s! Error: %s\n", host.c_str(), strerror(errno));
    #endif
    close();
  }else{
    if (nonblock){
      int flags = fcntl(sock, F_GETFL, 0);
      flags |= O_NONBLOCK;
      fcntl(sock, F_SETFL, flags);
    }
  }
}//Socket::Connection TCP Contructor

/// Returns the connected-state for this socket.
/// Note that this function might be slightly behind the real situation.
/// The connection status is updated after every read/write attempt, when errors occur
/// and when the socket is closed manually.
/// \returns True if socket is connected, false otherwise.
bool Socket::Connection::connected() const{
  return (sock >= 0) || ((pipes[0] >= 0) && (pipes[1] >= 0));
}

/// Returns total amount of bytes sent.
unsigned int Socket::Connection::dataUp(){
  return up;
}

/// Returns total amount of bytes received.
unsigned int Socket::Connection::dataDown(){
  return down;
}

/// Returns a std::string of stats, ended by a newline.
/// Requires the current connector name as an argument.
std::string Socket::Connection::getStats(std::string C){
  return getHost() + " " + C + " " + uint2string(time(0) - conntime) + " " + uint2string(up) + " " + uint2string(down) + "\n";
}

/// Updates the downbuffer and upbuffer internal variables.
/// Returns true if new data was received, false otherwise.
bool Socket::Connection::spool(){
  iwrite(upbuffer);
  return iread(downbuffer);
}

/// Updates the downbuffer and upbuffer internal variables until upbuffer is empty.
/// Returns true if new data was received, false otherwise.
bool Socket::Connection::flush(){
  while (upbuffer.size() > 0 && connected()){
    iwrite(upbuffer);
    usleep(5000);//sleep 5 ms
  }
  return iread(downbuffer);
}


/// Returns a reference to the download buffer.
std::string & Socket::Connection::Received(){
  return downbuffer;
}

/// Appends data to the upbuffer.
void Socket::Connection::Send(std::string data){
  upbuffer.append(data);
}

/// Incremental write call. This function tries to write len bytes to the socket from the buffer,
/// returning the amount of bytes it actually wrote.
/// \param buffer Location of the buffer to write from.
/// \param len Amount of bytes to write.
/// \returns The amount of bytes actually written.
int Socket::Connection::iwrite(const void * buffer, int len){
  if (!connected()){return 0;}
  int r;
  if (sock >= 0){
    r = send(sock, buffer, len, 0);
  }else{
    r = write(pipes[0], buffer, len);
  }
  if (r < 0){
    switch (errno){
      case EWOULDBLOCK:
        return 0;
        break;
      default:
        if (errno != EPIPE){
          Error = true;
          #if DEBUG >= 2
          fprintf(stderr, "Could not iwrite data! Error: %s\n", strerror(errno));
          #endif
        }
        close();
        return 0;
        break;
    }
  }
  if (r == 0 && (sock >= 0)){
    close();
  }
  up += r;
  return r;
}//Socket::Connection::iwrite

/// Incremental read call. This function tries to read len bytes to the buffer from the socket,
/// returning the amount of bytes it actually read.
/// \param buffer Location of the buffer to read to.
/// \param len Amount of bytes to read.
/// \returns The amount of bytes actually read.
int Socket::Connection::iread(void * buffer, int len){
  if (!connected()){return 0;}
  int r;
  if (sock >= 0){
    r = recv(sock, buffer, len, 0);
  }else{
    r = read(pipes[1], buffer, len);
  }
  if (r < 0){
    switch (errno){
      case EWOULDBLOCK:
        return 0;
        break;
      default:
        if (errno != EPIPE){
          Error = true;
          #if DEBUG >= 2
          fprintf(stderr, "Could not iread data! Error: %s\n", strerror(errno));
          #endif
        }
        close();
        return 0;
        break;
    }
  }
  if (r == 0 && (sock >= 0)){
    close();
  }
  down += r;
  return r;
}//Socket::Connection::iread

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

/// Gets hostname for connection, if available.
std::string Socket::Connection::getHost(){
  return remotehost;
}

/// Sets hostname for connection manually.
/// Overwrites the detected host, thus possibily making it incorrect.
void Socket::Connection::setHost(std::string host){
  remotehost = host;
}

/// Returns true if these sockets are the same socket.
/// Does not check the internal stats - only the socket itself.
bool Socket::Connection::operator== (const Connection &B) const{
  return sock == B.sock && pipes[0] == B.pipes[0] && pipes[1] == B.pipes[1];
}

/// Returns true if these sockets are not the same socket.
/// Does not check the internal stats - only the socket itself.
bool Socket::Connection::operator!= (const Connection &B) const{
  return sock != B.sock || pipes[0] != B.pipes[0] || pipes[1] != B.pipes[1];
}

/// Returns true if the socket is valid.
/// Aliases for Socket::Connection::connected()
Socket::Connection::operator bool() const{
  return connected();
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
  if (!IPv6bind(port, hostname, nonblock) && !IPv4bind(port, hostname, nonblock)){
    fprintf(stderr, "Could not create socket %s:%i! Error: %s\n", hostname.c_str(), port, strerror(errno));
    sock = -1;
  }
}//Socket::Server TCP Constructor

/// Attempt to bind an IPv6 socket.
/// \param port The TCP port to listen on
/// \param hostname The interface to bind to. The default is 0.0.0.0 (all interfaces).
/// \param nonblock Whether accept() calls will be nonblocking. Default is false (blocking).
/// \return True if successful, false otherwise.
bool Socket::Server::IPv6bind(int port, std::string hostname, bool nonblock){
  sock = socket(AF_INET6, SOCK_STREAM, 0);
  if (sock < 0){
    #if DEBUG >= 1
    fprintf(stderr, "Could not create IPv6 socket %s:%i! Error: %s\n", hostname.c_str(), port, strerror(errno));
    #endif
    return false;
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
  if (hostname == "0.0.0.0" || hostname.length() == 0){
    addr.sin6_addr = in6addr_any;
  }else{
    inet_pton(AF_INET6, hostname.c_str(), &addr.sin6_addr);//set interface, 0.0.0.0 (default) is all
  }
  int ret = bind(sock, (sockaddr*)&addr, sizeof(addr));//do the actual bind
  if (ret == 0){
    ret = listen(sock, 100);//start listening, backlog of 100 allowed
    if (ret == 0){
      #if DEBUG >= 1
      fprintf(stderr, "IPv6 socket success @ %s:%i\n", hostname.c_str(), port);
      #endif
      return true;
    }else{
      #if DEBUG >= 1
      fprintf(stderr, "IPv6 Listen failed! Error: %s\n", strerror(errno));
      #endif
      close();
      return false;
    }
  }else{
    #if DEBUG >= 1
    fprintf(stderr, "IPv6 Binding %s:%i failed (%s)\n", hostname.c_str(), port, strerror(errno));
    #endif
    close();
    return false;
  }
}

/// Attempt to bind an IPv4 socket.
/// \param port The TCP port to listen on
/// \param hostname The interface to bind to. The default is 0.0.0.0 (all interfaces).
/// \param nonblock Whether accept() calls will be nonblocking. Default is false (blocking).
/// \return True if successful, false otherwise.
bool Socket::Server::IPv4bind(int port, std::string hostname, bool nonblock){
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0){
    #if DEBUG >= 1
    fprintf(stderr, "Could not create IPv4 socket %s:%i! Error: %s\n", hostname.c_str(), port, strerror(errno));
    #endif
    return false;
  }
  int on = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  if (nonblock){
    int flags = fcntl(sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);
  }
  struct sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  addr4.sin_port = htons(port);//set port
  if (hostname == "0.0.0.0" || hostname.length() == 0){
    addr4.sin_addr.s_addr = INADDR_ANY;
  }else{
    inet_pton(AF_INET, hostname.c_str(), &addr4.sin_addr);//set interface, 0.0.0.0 (default) is all
  }
  int ret = bind(sock, (sockaddr*)&addr4, sizeof(addr4));//do the actual bind
  if (ret == 0){
    ret = listen(sock, 100);//start listening, backlog of 100 allowed
    if (ret == 0){
      #if DEBUG >= 1
      fprintf(stderr, "IPv4 socket success @ %s:%i\n", hostname.c_str(), port);
      #endif
      return true;
    }else{
      #if DEBUG >= 1
      fprintf(stderr, "IPv4 Listen failed! Error: %s\n", strerror(errno));
      #endif
      close();
      return false;
    }
  }else{
    #if DEBUG >= 1
    fprintf(stderr, "IPv4 binding %s:%i failed (%s)\n", hostname.c_str(), port, strerror(errno));
    #endif
    close();
    return false;
  }
}

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
    if ((errno != EWOULDBLOCK) && (errno != EAGAIN) && (errno != EINTR)){
      #if DEBUG >= 1
      fprintf(stderr, "Error during accept - closing server socket.\n");
      #endif
      close();
    }
  }else{
    if (addrinfo.sin6_family == AF_INET6){
      tmp.remotehost = inet_ntop(AF_INET6, &(addrinfo.sin6_addr), addrconv, INET6_ADDRSTRLEN);
      #if DEBUG >= 6
      fprintf(stderr,"IPv6 addr: %s\n", tmp.remotehost.c_str());
      #endif
    }
    if (addrinfo.sin6_family == AF_INET){
      tmp.remotehost = inet_ntop(AF_INET, &(((sockaddr_in*)&addrinfo)->sin_addr), addrconv, INET6_ADDRSTRLEN);
      #if DEBUG >= 6
      fprintf(stderr,"IPv4 addr: %s\n", tmp.remotehost.c_str());
      #endif
    }
    if (addrinfo.sin6_family == AF_UNIX){
      #if DEBUG >= 6
      tmp.remotehost = ((sockaddr_un*)&addrinfo)->sun_path;
      fprintf(stderr,"Unix socket, no address\n");
      #endif
      tmp.remotehost = "UNIX_SOCKET";
    }
  }
  return tmp;
}

/// Close connection. The internal socket is closed and then set to -1.
/// If the connection is already closed, nothing happens.
void Socket::Server::close(){
  if (connected()){
    #if DEBUG >= 6
    fprintf(stderr, "ServerSocket closed.\n");
    #endif
    shutdown(sock, SHUT_RDWR);
    ::close(sock);
    sock = -1;
  }
}//Socket::Server::close

/// Returns the connected-state for this socket.
/// Note that this function might be slightly behind the real situation.
/// The connection status is updated after every accept attempt, when errors occur
/// and when the socket is closed manually.
/// \returns True if socket is connected, false otherwise.
bool Socket::Server::connected() const{
  return (sock >= 0);
}//Socket::Server::connected

/// Returns internal socket number.
int Socket::Server::getSocket(){return sock;}
