// C++ Socket Wrapper
// SocketW base socket header 
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
 
#ifndef sw_base_H
#define sw_base_H

#include "sw_internal.h"

#include <unistd.h>
#include <string>

// Set error handling mode
// throw_errors == true  : Throws the error class on unhandled errors
// throw_errors == false : Exit on unhandled errors
// verbose == true       : Prints the error message to stderr on unhandled errors
//
// Default is throw_errors == false and verbose == true
void sw_setThrowMode(bool throw_errors);
void sw_setVerboseMode(bool verbose);
bool sw_getThrowMode(void);
bool sw_getVerboseMode(void);


// Abstract base class for streaming sockets
class DECLSPEC SWBaseSocket
{
public:	
	SWBaseSocket();
	virtual ~SWBaseSocket();
	
	// Error types
	// ok           - operation succesful
	// fatal        - unspecified error
	// notReady     - you should call the function again
	//                indicates that the function would block (if nowait/nonblocking)
	// portInUse    - this port is used by another socket ( on listen() )
	// notConnected - socket not connected (or valid)
	// msgTooLong   - the message size it too big for send()
	// terminated   - connection terminated (by peer)
	// noResponse   - can't connect() to peer
	// timeout      - a read/write operation timed out (only if a timeout value is set and if in blocking mode)
	// interrupted  - operation was interrupted by a nonblocked signal
	enum base_error{ok, fatal, notReady, portInUse, notConnected, msgTooLong, terminated, noResponse, timeout, interrupted};
	
	class DECLSPEC SWBaseError
	{
	public:
		SWBaseError();
		SWBaseError(base_error e);
		
		virtual ~SWBaseError(){;}
		
		virtual std::string get_error();
		virtual SWBaseSocket* get_failedClass(void);
		
		virtual bool operator==(SWBaseError e);
		virtual bool operator!=(SWBaseError e);
		
		virtual void set_errorString(std::string msg);
		virtual void set_failedClass(SWBaseSocket *pnt);
	protected:
		friend class SWBaseSocket;
		
		// The base error type
		base_error be;
		
		// Human readable error string
		std::string error_string;
		
		// A pointer to the class causing the error
		SWBaseSocket *failed_class;
	};
	
	
	// Note: If no SWBaseError class is provided with a method call (==NULL),
	// SocketW will print the error to stderr and exit or throw on errors.
	
	// Note: All bool functions returns true on success.
	
	// Block mode
	// blocking    - everythings blocks until completly done
	// noWait      - operations block but only once
	//               useful with blocking w. select()
	// nonblocking - don't block (you should use select())
	enum block_type{nonblocking, noWait, blocking};
	
	
	// Connection methods
	// qLimit - the maximum length the queue of pending connections.
	// Accept returns a new socket class connected with peer (should be
	// freed with delete) or NULL on failure. You can cast the class to
	// the correct type if you need to ( eg. (SWInetSocket *)mysocket ).
	virtual bool listen(int qLimit = 5, SWBaseError *error = NULL);
	virtual SWBaseSocket* accept(SWBaseError *error = NULL);
	// bind() and connect() are implemented in child classes
	
	// do the disconnect ritual (signal peer, wait for close singal and close socket)
	virtual bool disconnect(SWBaseError *error = NULL);
	
	// force close socket
	virtual bool close_fd();  //use with care, disconnect() is cleaner

	// Direct I/O (raw)
	// Can send/recv less bytes than specified!
	// Returns the actual amount of bytes sent/recv on sucess
	// and an negative integer on failure.
	virtual int send(const char *buf, int bytes, SWBaseError *error = NULL);
	virtual int sendmsg(const std::string msg, SWBaseError *error = NULL);
	virtual int recv(char *buf, int bytes, SWBaseError *error = NULL);
	virtual std::string recvmsg(int bytes = 256, SWBaseError *error = NULL);
	
	// Forced I/O
	// Force system to send/recv the specified amount of bytes.
	// On nowait/nonblocking: might return with notReady and then you
	// MUST call the same method again (eg. wait with select() to know when) 
	// with the same parameters until the operation is finished.
	// Returns 'bytes' when finished, negative integer on failure and
	// 'notReady'. In the 'notReady' case, -(return value) is the amount of
	// bytes sent/recv so far.
	virtual int fsend(const char *buf, int bytes, SWBaseError *error = NULL);
	virtual int fsendmsg(const std::string msg, SWBaseError *error = NULL);
	virtual int frecv(char *buf, int bytes, SWBaseError *error = NULL);
	
	// Tools
	// get_fd() - get socket descriptor, can be used with select()
	// returns -1 on failure.
	// get_host/peer fills the provided structures with info about the
	// host/peer (see man unix & ip).
	// SWInetSocket has some more tools for TCP/IP sockets.
	virtual int get_fd(SWBaseError *error);
	virtual bool get_host(sockaddr *host, SWBaseError *error = NULL);
	virtual bool get_peer(sockaddr *peer, SWBaseError *error = NULL);
	
	// Set recv timeout (only in blocking mode).
	// set_timeout(0,0) means wait forever (default).
	// This affects the functions recv(), send(), accept() and disconnect()
	// and others that use those, i.e. all frecvmsg().
	void set_timeout(Uint32 sec, Uint32 usec){ tsec = sec, tusec = usec; }
	
	// Error handling
	virtual void print_error(); //prints the last error if any to stderr
	virtual std::string get_error(){return error_string;}  //returns a human readable error string

protected:
	// get a new socket if myfd < 0
	virtual void get_socket()=0;
	
	// create a new class for accept() using socketdescriptor
	virtual SWBaseSocket* create(int socketdescriptor, SWBaseError *error)=0;

	// reset state
	virtual void reset();

	// wait for I/O (with timeout)
	enum io_type{read, write, except, rw, all};
	virtual bool waitIO(io_type &type, SWBaseError *error);
	bool waitRead(SWBaseError *error);
	bool waitWrite(SWBaseError *error);

	// internal error handling
	virtual void handle_errno(SWBaseError *error, std::string msg);
	virtual void no_error(SWBaseError *error);
	virtual void set_error(SWBaseError *error, SWBaseError name, std::string msg);

	// our socket descriptor
	int myfd;
	
	// last error
	std::string error_string;

	// data for fsend
	bool fsend_ready;
	int fsend_total;
	int fsend_bytesleft;
	
	// data for frecv
	bool frecv_ready;
	int frecv_total;
	int frecv_bytesleft;
		
	// have we recived a shutdown signal?
	bool recv_close;

	//blocking mode (set by child classes)
	block_type block_mode;
	
	//timeout for waitIO()
	int tsec;
	int tusec;
};


#endif /* sw_base_H */
