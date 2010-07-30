// C++ Socket Wrapper 
// SocketW Unix socket header
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

#ifndef sw_unix_H
#define sw_unix_H
#ifndef __WIN32__

#include "sw_internal.h"
#include "sw_base.h"
#include <string>

// Simple streaming Unix class
class DECLSPEC SWUnixSocket : public SWBaseSocket
{
public:
	SWUnixSocket(block_type block=blocking);
	~SWUnixSocket();

	// bind and connect to the socket file "path"
	virtual bool bind(std::string path, SWBaseError *error = NULL);
	virtual bool connect(std::string path, SWBaseError *error = NULL);

protected:
	virtual void get_socket();
	virtual SWBaseSocket* create(int socketdescriptor, SWBaseError *error);
};

#endif /* __WIN32__ */
#endif /* sw_unix_H */
