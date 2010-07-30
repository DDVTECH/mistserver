// C++ Socket Wrapper
// SocketW Unix socket
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
 
#include "sw_unix.h"
#include <fcntl.h>

using namespace std;

//====================================================================
//== SWUnixSocket
//== Unix streaming sockets
//====================================================================
#ifndef __WIN32__

SWUnixSocket::SWUnixSocket(block_type block)
{	
	block_mode = block;
}

SWUnixSocket::~SWUnixSocket()
{
	//nothing here
}

void SWUnixSocket::get_socket()
{
	if( myfd < 0 ){
		myfd = socket(PF_UNIX, SOCK_STREAM, 0);
	
		if( block_mode == nonblocking )
			fcntl(myfd, F_SETFL, O_NONBLOCK);
		
		//reset state	
		reset();
	}
}


SWBaseSocket* SWUnixSocket::create(int socketdescriptor, SWBaseError *error)
{
	SWUnixSocket* remoteClass;
		
	/* Create new class*/
	remoteClass = new SWUnixSocket(block_mode);
	remoteClass->myfd = socketdescriptor;
	
	no_error(error);
	return remoteClass;
}

bool SWUnixSocket::bind(string path, SWBaseError *error)
{
	get_socket();
	
	sockaddr_un myAdr;

	myAdr.sun_family = AF_UNIX;
	strncpy(myAdr.sun_path, path.c_str(), path.size()+1);

	if(::bind(myfd, (sockaddr *)&myAdr, sizeof(myAdr)) == -1){
		handle_errno(error, "SWUnixSocket::bind() error: ");
		return false;
	}
		
	no_error(error);
	return true;
}

bool SWUnixSocket::connect(string path, SWBaseError *error)
{
	get_socket();

	sockaddr_un remoteAdr;
	
	remoteAdr.sun_family = AF_UNIX;
	strncpy(remoteAdr.sun_path, path.c_str(), path.size()+1);

	if(::connect(myfd, (sockaddr *)&remoteAdr, sizeof(remoteAdr)) == -1){
		handle_errno(error, "SWUnixSocket::connect() error: ");
		return false;
	}

	no_error(error);
	return true;
}

#endif /* __WIN32__ */
