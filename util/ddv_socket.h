/// \file ddv_socket.h
/// Holds all headers for the DDV namespace.

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


///Holds DDV Socket tools.
namespace DDV{

  /// This class is for easy communicating through sockets, either TCP or Unix.
  class Socket{
    private:
      int sock; ///< Internally saved socket number.
    public:
      Socket(); ///< Create a new disconnected base socket.
      Socket(int sockNo); ///< Create a new base socket.
      Socket(std::string adres, bool nonblock = false); ///< Create a new Unix Socket.
      bool Error; ///< Set to true if a socket error happened.
      bool Blocking; ///< Set to true if a socket is currently or wants to be blocking.
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
      void close(); ///< Close connection.
      int getSocket(); ///< Returns internal socket number.
  };

  /// This class is for easily setting up listening socket, either TCP or Unix.
  class ServerSocket{
    private:
      int sock; ///< Internally saved socket number.
    public:
      ServerSocket(); ///< Create a new base ServerSocket.
      ServerSocket(int port, std::string hostname = "0.0.0.0", bool nonblock = false); ///< Create a new TCP ServerSocket.
      ServerSocket(std::string adres, bool nonblock = false); ///< Create a new Unix ServerSocket.
      Socket accept(bool nonblock = false); ///< Accept any waiting connections.
      bool connected(); ///< Returns the connected-state for this socket.
      void close(); ///< Close connection.
      int getSocket(); ///< Returns internal socket number.
  };
  
};
