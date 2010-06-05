// C++ Socket Wrapper 
// SocketW Inet socket header
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

#ifndef sw_inet_H
#define sw_inet_H

#include "sw_internal.h"
#include "sw_base.h"
#include <string>

// Simple streaming TCP/IP class
class DECLSPEC SWInetSocket : public SWBaseSocket
{
public:
	SWInetSocket(block_type block=blocking);
	virtual ~SWInetSocket();
	
	virtual bool bind(int port, SWBaseError *error = NULL);  //use port=0 to get any free port
	virtual bool bind(int port, std::string host, SWBaseError *error = NULL); //you can also specify the host interface to use
	virtual bool connect(int port, std::string hostname, SWBaseError *error = NULL);
	
	// Tools
	// Gets IP addr, name or port.
	virtual std::string get_peerAddr(SWBaseError *error = NULL);
	virtual int get_peerPort(SWBaseError *error = NULL);
	virtual std::string get_peerName(SWBaseError *error = NULL);
	virtual std::string get_hostAddr(SWBaseError *error = NULL);
	virtual int get_hostPort(SWBaseError *error = NULL);
	virtual std::string get_hostName(SWBaseError *error = NULL);
	
protected:	
	virtual void get_socket();
	virtual SWBaseSocket* create(int socketdescriptor, SWBaseError *error);
};

#endif /* sw_inet_H */
