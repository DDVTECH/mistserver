// C++ Socket Wrapper
// SocketW Inet socket
//
// Started 020316
//
// License: LGPL v2.1+ (see the file LICENSE)
// (c)2002-2003 Anders Lindström

/***********************************************************************
 *  This library is free software; you can redistribute it and/or      *
 *  modify it under the terms of the GNU Lesser General Public         *
 *  License as published by the Free Software Foundation; either       *
 *  version 2.1 of the License, or (at your option) any later version. *
 ***********************************************************************/

#include "sw_inet.h"

#ifndef __WIN32__
  #include <netdb.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <sys/select.h>
  #include <sys/time.h>
#else
  #define F_SETFL FIONBIO
  #define O_NONBLOCK 1
  
  //Defined in sw_base.cxx
  extern int close(int fd);
  extern int fcntl(int fd, int cmd, long arg);
#endif

using namespace std;

//====================================================================
//== SWInetSocket
//== Inet (TCP/IP) streaming sockets
//====================================================================
SWInetSocket::SWInetSocket(block_type block)
{
	block_mode = block;
}

SWInetSocket::~SWInetSocket()
{

}

void SWInetSocket::get_socket()
{
	if( myfd < 0 ){
		myfd = socket(PF_INET, SOCK_STREAM, 0);
	
		if( block_mode == nonblocking )
			fcntl(myfd, F_SETFL, O_NONBLOCK);
			
		//reset state	
		reset();
	}
}

SWBaseSocket* SWInetSocket::create(int socketdescriptor, SWBaseError *error)
{
	SWInetSocket* remoteClass;
		
	/* Create new class*/
	remoteClass = new SWInetSocket(block_mode);
	remoteClass->myfd = socketdescriptor;
	
	no_error(error);
	return remoteClass;
}

bool SWInetSocket::bind(int port, SWBaseError *error)
{
	return bind(port, "", error);
}

bool SWInetSocket::bind(int port, string host, SWBaseError *error)
{
	hostent *h;
	in_addr inp; 	

 	if( host.size() > 0 ){
		// Bind to a specific address
	
		if( (h = gethostbyname(host.c_str())) == NULL ){
 			set_error(error, fatal, "SWInetSocket::bind() - Can't get host by name");
 			return false;
 		}
		
		inp = *((in_addr *)h->h_addr);
	}else{
		// Bind to any
		inp.s_addr = INADDR_ANY;
	}
	

	get_socket();

	sockaddr_in myAdr;

	memset(&myAdr, 0, sizeof(myAdr));
	myAdr.sin_family = AF_INET;
	myAdr.sin_port = htons(port);
	myAdr.sin_addr.s_addr = inp.s_addr;

	if(::bind(myfd, (sockaddr *)&myAdr, sizeof(myAdr)) == -1){
		handle_errno(error, "SWInetSocket::bind() error: ");
		return false;
	}
		
	no_error(error);
	return true;
}

bool SWInetSocket::connect(int port, string hostname, SWBaseError *error)
{
	get_socket();

	hostent *host;

	if( (host = gethostbyname(hostname.c_str())) == NULL ){
		set_error(error, fatal, "SWInetSocket::connect() - Can't get host by name");
		return false;
	}

	sockaddr_in remoteAdr;

	memset(&remoteAdr, 0, sizeof(remoteAdr));
	remoteAdr.sin_family = AF_INET;
	remoteAdr.sin_port = htons(port);
	remoteAdr.sin_addr = *((in_addr *)host->h_addr);

	if(::connect(myfd, (sockaddr *)&remoteAdr, sizeof(remoteAdr)) == -1){
		handle_errno(error, "SWInetSocket::connect() error: ");
		return false;
	}

	no_error(error);
	return true;
}


string SWInetSocket::get_peerAddr(SWBaseError *error)
{
	sockaddr_in adr;
	
	if( !get_peer((sockaddr *)&adr, error) )
		return "";
	
	char *pnt;
	
	if( (pnt = inet_ntoa(adr.sin_addr)) == NULL ){
		set_error(error, fatal, "SWInetSocket::get_peerName() - Can't get peer address");
		return "";
	}
	string name(pnt);
	
	no_error(error);
	return name;
}

int SWInetSocket::get_peerPort(SWBaseError *error)
{
	sockaddr_in adr;
	
	if( !get_peer((sockaddr *)&adr, error) )
		return -1;
	
	no_error(error);
	
	return ntohs(adr.sin_port);
}

string SWInetSocket::get_peerName(SWBaseError *error)
{
	string name = get_peerAddr(error);
	if(name.size() < 1)
 		return "";

	
	hostent *h; 	

 	if( (h = gethostbyname(name.c_str())) == NULL ){
 		set_error(error, fatal, "SWInetSocket::get_peerName() - Can't get peer by address");
 		return "";
 	}
	string host_name(h->h_name);
	
	no_error(error);
	return host_name;
}

string SWInetSocket::get_hostAddr(SWBaseError *error)
{
	//We need to get the real address, so we must
	//first get this computers host name and then
	//translate that into an address!

	string name = get_hostName(error);
	if( name.size() < 1 )
		return "";
		
	hostent *host;

	if( (host = gethostbyname(name.c_str())) == NULL ){
		set_error(error, fatal, "SWInetSocket::get_hostAddr() - Can't get host by name");
		return "";
	}
	
	char *pnt;
	
	if( (pnt = inet_ntoa(*((in_addr *)host->h_addr))) == NULL){
		set_error(error, fatal, "SWInetSocket::get_hostAddr() - Can't get host address");
		return "";
	}
	
	string adr(pnt);
	
	return adr;
}

int SWInetSocket::get_hostPort(SWBaseError *error)
{
	sockaddr_in adr;
	
	if( !get_host((sockaddr *)&adr, error) )
		return -1;
	
	no_error(error);
	
	return ntohs(adr.sin_port);
}

string SWInetSocket::get_hostName(SWBaseError *error)
{
	char buf[256];
	
	if( gethostname(buf, 256) != 0 ){
		handle_errno(error, "SWInetSocket::gethostname() error: ");
		return "";
	}
	
	string msg(buf);
	
	no_error(error);
	return msg;
}
