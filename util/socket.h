/// \file socket.h
/// A handy Socket wrapper library.
/// Written by Jaron Vietor in 2010 for DDVTech

#pragma once
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>


///Holds Socket tools.
namespace Socket{

  /// This class is for easy communicating through sockets, either TCP or Unix.
  class Connection{
    private:
      int sock; ///< Internally saved socket number.
      std::string remotehost; ///< Stores remote host address.
      unsigned int up;
      unsigned int down;
      unsigned int conntime;
    public:
      Connection(); ///< Create a new disconnected base socket.
      Connection(int sockNo); ///< Create a new base socket.
      Connection(std::string hostname, int port, bool nonblock); ///< Create a new TCP socket.
      Connection(std::string adres, bool nonblock = false); ///< Create a new Unix Socket.
      bool canRead(); ///< Calls poll() on the socket, checking if data is available.
      bool canWrite(); ///< Calls poll() on the socket, checking if data can be written.
      signed int ready(); ///< Returns the ready-state for this socket.
      bool connected(); ///< Returns the connected-state for this socket.
      bool read(void * buffer, int len); ///< Reads data from socket.
      bool read(void * buffer, int width, int count); ///< Read call that is compatible with file access syntax.
      bool write(const void * buffer, int len); ///< Writes data to socket.
      bool write(void * buffer, int width, int count); ///< Write call that is compatible with file access syntax.
      bool write(const std::string data); ///< Write call that is compatible with std::string.
      int iwrite(void * buffer, int len); ///< Incremental write call.
      int iread(void * buffer, int len); ///< Incremental read call.
      bool read(std::string & buffer); ///< Read call that is compatible with std::string.
      bool swrite(std::string & buffer); ///< Read call that is compatible with std::string.
      bool iread(std::string & buffer); ///< Incremental write call that is compatible with std::string.
      bool iwrite(std::string & buffer); ///< Write call that is compatible with std::string.
      void close(); ///< Close connection.
      std::string getHost(); ///< Gets hostname for connection, if available.
      int getSocket(); ///< Returns internal socket number.
      std::string getError(); ///< Returns a string describing the last error that occured.
      unsigned int dataUp(); ///< Returns total amount of bytes sent.
      unsigned int dataDown(); ///< Returns total amount of bytes received.
      std::string getStats(std::string C); ///< Returns a std::string of stats, ended by a newline.
      friend class Server;
      bool Error; ///< Set to true if a socket error happened.
      bool Blocking; ///< Set to true if a socket is currently or wants to be blocking.
  };

  /// This class is for easily setting up listening socket, either TCP or Unix.
  class Server{
    private:
      int sock; ///< Internally saved socket number.
    public:
      Server(); ///< Create a new base Server.
      Server(int port, std::string hostname = "0.0.0.0", bool nonblock = false); ///< Create a new TCP Server.
      Server(std::string adres, bool nonblock = false); ///< Create a new Unix Server.
      Connection accept(bool nonblock = false); ///< Accept any waiting connections.
      bool connected(); ///< Returns the connected-state for this socket.
      void close(); ///< Close connection.
      int getSocket(); ///< Returns internal socket number.
  };

};
