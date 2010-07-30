// C++ Socket Wrapper
// SocketW base class
//
// Started 020316
//
// License: LGPL v2.1+ (see the file LICENSE)
// (c)2002-2003 Anders Lindstr�m

/***********************************************************************
 *  This library is free software; you can redistribute it and/or      *
 *  modify it under the terms of the GNU Lesser General Public         *
 *  License as published by the Free Software Foundation; either       *
 *  version 2.1 of the License, or (at your option) any later version. *
 ***********************************************************************/
 
#include "sw_base.h"
#include <errno.h>
#include <new>
#include <time.h>
#include <stdio.h>
#include <string.h>

#ifndef __WIN32__
  #include <netdb.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <sys/select.h>
  #include <sys/time.h>
  
  #define INVALID_SOCKET -1  //avoid M$ braindamage
#else
  //why use POSIX standards when we can make our own
  //and frustrate people? (well known M$ policy)
  
  #ifndef EBADF
    #define EBADF WSAEBADF
  #endif
  
  #define ENOTSOCK WSAENOTSOCK
  #define EOPNOTSUPP WSAEOPNOTSUPP
  #define EADDRINUSE WSAEADDRINUSE
  #define EWOULDBLOCK WSAEWOULDBLOCK
  #define EMSGSIZE WSAEMSGSIZE
  #define EINPROGRESS WSAEINPROGRESS
  #define EALREADY WSAEALREADY
  #define ECONNREFUSED WSAECONNREFUSED
  #define ETIMEDOUT WSAETIMEDOUT
  #define ENOTCONN WSAENOTCONN
  
  #ifndef EINTR
    #define EINTR WSAEINTR
  #endif
#endif

#ifndef MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
#endif

// Socklen hack
#if defined(__linux__) || defined(__FreeBSD__) // || defined(__bsdi__) || defined(__NetBSD__) too, perhaps? Bugreports, please!
  #define sw_socklen_t socklen_t
#elif defined(__WIN32__) || defined(__osf__)
  #define sw_socklen_t int
#else
  #define sw_socklen_t unsigned int
#endif


using namespace std;

#ifdef __WIN32__
//Win32 braindamage
int close(int fd)
{
	return closesocket(fd);
}

int fcntl(int fd, int cmd, long arg)
{
	unsigned long mode = arg;

	return WSAIoctl(fd, cmd, &mode, sizeof(unsigned long), NULL, 0, NULL, NULL, NULL);
}

void WSA_exit(void)
{
	WSACleanup();
}
#endif


//====================================================================
//== Error handling mode
//====================================================================
bool sw_DoThrow = false;
bool sw_Verbose = true;

void sw_setThrowMode(bool throw_errors)
{
	sw_DoThrow = throw_errors;
}

void sw_setVerboseMode(bool verbose)
{
	sw_Verbose = verbose;
}

bool sw_getThrowMode(void)
{
	return sw_DoThrow;
}

bool sw_getVerboseMode(void)
{
	return sw_Verbose;
}


//====================================================================
//== Base error class
//====================================================================
SWBaseSocket::SWBaseError::SWBaseError()
{
	be = ok;
	error_string = "";
	failed_class = NULL;
}

SWBaseSocket::SWBaseError::SWBaseError(base_error e)
{
	be = e;
	error_string = "";
	failed_class = NULL;
}

string SWBaseSocket::SWBaseError::get_error()
{
	return error_string;
}

SWBaseSocket* SWBaseSocket::SWBaseError::get_failedClass(void)
{
	return failed_class;
}

void SWBaseSocket::SWBaseError::set_errorString(string msg)
{ 
	error_string = msg;
}

void SWBaseSocket::SWBaseError::set_failedClass(SWBaseSocket *pnt)
{
	failed_class = pnt;
}

bool SWBaseSocket::SWBaseError::operator==(SWBaseError e)
{
	return be == e.be;
}
		
bool SWBaseSocket::SWBaseError::operator!=(SWBaseError e)
{
	return be != e.be;
}
	

//====================================================================
//== SWBaseSocket
//== Base class for sockets
//====================================================================
SWBaseSocket::SWBaseSocket()
{
	//indicate nonopen
	myfd = -1;
	recv_close = false;	
	
	//init values
	error_string = "";
	block_mode = blocking;
	fsend_ready = true;
	frecv_ready = true;
	tsec = 0;
	tusec = 0;
	
	#ifdef __WIN32__
	//kick winsock awake
	static bool firstuse = true;
	if( firstuse == true ){
		WSAData wsaData;
		int nCode;
    	if( (nCode = WSAStartup(MAKEWORD(1, 1), &wsaData)) != 0 ){
			handle_errno(NULL, "SWBaseSocket - WSAStartup() failed: ");
        	exit(-1);  // Should never happend
    	}
		
		//cleanup at exit
		atexit(WSA_exit);
		firstuse = false;
	}
	#endif /* __WIN32__ */
}

SWBaseSocket::~SWBaseSocket()
{	
	if(myfd > 0)
		close(myfd);
}

bool SWBaseSocket::listen(int qLimit, SWBaseError *error)
{
	get_socket();

	//Avoid "Address already in use" thingie
	char yes=1;
	setsockopt(myfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	
	if(::listen(myfd, qLimit) == -1){		
		handle_errno(error, "SWBaseSocket::listen() error: ");
		return false;
	}

	no_error(error);
	return true;
}

SWBaseSocket* SWBaseSocket::accept(SWBaseError *error)
{
	int remotefd = -1;
	sockaddr remoteAdr;

	if( !waitRead(error) )
		return NULL;
	
	sw_socklen_t ssize = sizeof(sockaddr);

	if((remotefd = ::accept(myfd, &remoteAdr, &ssize)) == int(INVALID_SOCKET)){
		handle_errno(error, "SWBaseSocket::accept() error: ");
		return NULL;
	}
	
	//nonblocking?
	if( block_mode == nonblocking )
		fcntl(remotefd, F_SETFL, O_NONBLOCK);
	
	/* Create new class*/
	SWBaseSocket* remoteClass = create(remotefd, error);
	if( remoteClass == NULL )
		return NULL;
	
	no_error(error);
	return remoteClass;
}

bool SWBaseSocket::disconnect(SWBaseError *error)
{
	int n = 0;
	char buf[256];

	if(myfd < 0){
		set_error(error, notConnected, "SWBaseSocket::disconnect() - No connection");
		return false;
	}

	//close WR (this signals the peer) 
	if( shutdown(myfd, 1) != 0 ){
		handle_errno(error, "SWBaseSocket::disconnect() error: ");
		return false;
	}
	

	SWBaseError err;

	//wait for close signal from peer
	if( recv_close == false ){
		while(true){
			if( !waitRead(error) )
				return false;
			
			n = recv(buf, 256, &err);
			
			if( n <= 0 )
				break;
			if(block_mode == noWait){
				//we don't want to block
				set_error(error, notReady, "SWBaseSocket::disconnect() - Need more time, call again");
				return false;
			}
		}
	}

	if( n != 0 ){
		set_error(error, err, error_string);
		return false; //error
	}
	
	//reset state
	reset();
	
	close(myfd);
	myfd = -1;
	
	no_error(error);
	return true;
}

bool SWBaseSocket::close_fd()
{
	if( myfd > 0 ){
		close(myfd);
		myfd = -1;
		
		//reset state
		reset();
		
		return true;
	}
	return false;
}

int SWBaseSocket::send(const char *buf, int bytes, SWBaseError *error)
{	
	int ret;

	if(myfd < 0){
		set_error(error, notConnected, "SWBaseSocket::send() - No connection");
		return -1;
	}
	
	if( !waitWrite(error) )
		return -1;
		
	ret = ::send(myfd, buf, bytes, MSG_NOSIGNAL);
	
	if( ret < 0 )
		handle_errno(error, "SWBaseSocket::send() error: ");
	else
		no_error(error);
	
	return ret;
}

int SWBaseSocket::fsend(const char *buf, int bytes, SWBaseError *error)
{
	int n;
	int bytessent;
	
	if(fsend_ready){
		//First call
		fsend_bytesleft =  bytes;
		fsend_total = fsend_bytesleft;  //global var needed for resume
		bytessent = 0;
		fsend_ready = false;            //point of no return
	}
	else{
		//resume
		bytessent = fsend_total - fsend_bytesleft;
	}
	
	//send package
	while( fsend_bytesleft > 0 ){	
		n = send( buf + bytessent , fsend_bytesleft, error );
		
		//return on error, wouldblock or nowait
		if( n < 0 )
			return ( (bytessent > 0 )? -bytessent : -1 );
			
		bytessent += n;
		fsend_bytesleft -= n;
		
		if ( block_mode == noWait  &&  fsend_bytesleft > 0 ){
			set_error(error, notReady, "SWBaseSocket::fsend() - Need more time, call again");
			return -bytessent;
		}
	} 
	
	fsend_ready = true;
	
	no_error(error);
	return fsend_total;
}

int SWBaseSocket::sendmsg(const string msg, SWBaseError *error)
{	
	return send(msg.c_str(), msg.size(), error);
}

int SWBaseSocket::fsendmsg(const string msg, SWBaseError *error)
{	
	return fsend(msg.c_str(), msg.size(), error);
}

int SWBaseSocket::recv(char *buf, int bytes, SWBaseError *error)
{
	int ret;

	if(myfd < 0){
		set_error(error, notConnected, "SWBaseSocket::recv() - No connection");
		return -1;
	}
	
	if( !waitRead(error) )
		return -1;
	
 	ret = ::recv(myfd, buf, bytes, MSG_NOSIGNAL);

	if( ret < 0 )
		handle_errno(error, "SWBaseSocket::recv() error: ");
	else if( ret == 0 ){
		recv_close = true;  //we  recived a close signal from peer
		set_error(error, terminated, "SWBaseSocket::recv() - Connection terminated by peer");	
	}else
		no_error(error);

	return ret;
}

int SWBaseSocket::frecv(char *buf, int bytes, SWBaseError *error)
{	
	int n;
	int bytesrecv;
	
	if(frecv_ready){
		//First call
		frecv_bytesleft =  bytes;
		frecv_total = frecv_bytesleft;  //global var needed for resume
		bytesrecv = 0;
		frecv_ready = false;            //point of no return
	}
	else{
		//resume            
		bytesrecv = frecv_total - frecv_bytesleft;
	}
	
	
	//recv package
	while( frecv_bytesleft > 0 ){
		n = recv( buf + bytesrecv , frecv_bytesleft, error );
		
		//return on error, wouldblock, nowait or timeout
		if( n < 0 )
			return ( (bytesrecv > 0 )? -bytesrecv : -1 );
		if( n == 0 )
			return 0;  // terminated
			
		bytesrecv += n;
		frecv_bytesleft -= n;
		
		if ( block_mode == noWait  &&  frecv_bytesleft > 0 ){
			set_error(error, notReady, "SWBaseSocket::frecv() - Need more time, call again");
			return -bytesrecv;
		}
	} 
	
	frecv_ready = true;
	
	no_error(error);
	return frecv_total;
}

string SWBaseSocket::recvmsg(int bytes, SWBaseError *error)
{
	char *buf = new char[bytes+1];

	SWBaseError err;
	string msg = "";
	int ret = recv(buf, bytes, &err);
	
	if( ret > 0 ){
		buf[ret]='\0';  // Make sure the string is null terminated
		msg = buf;
		no_error(error);
	}
	delete[] buf;
	
	if( ret < 1 )
		set_error(error, err, err.get_error());
	
	return msg;
}

int SWBaseSocket::get_fd(SWBaseError *error)
{
	if( myfd > 0 ){
		no_error(error);
		return myfd;
	}
	
	set_error(error, notConnected, "SWBaseSocket::get_fd() - No descriptor");		
	return -1;
}

bool SWBaseSocket::get_host(sockaddr *host, SWBaseError *error)
{
	if( host == NULL){
		set_error(error, fatal, "SWBaseSocket::get_host() - Got NULL pointer");
		return false;
	}

	if(myfd < 0){
		set_error(error, notConnected, "SWBaseSocket::get_host() - No socket");
		return false;
	}
	
	sw_socklen_t tmp = sizeof(sockaddr);
	if( getsockname(myfd, host, &tmp) != 0 ){
		handle_errno(error, "SWBaseSocket::get_host() error: ");
		return false;
	}
	
	no_error(error);
	return true;
}

bool SWBaseSocket::get_peer(sockaddr *peer, SWBaseError *error)
{
	if( peer == NULL){
		set_error(error, fatal, "SWBaseSocket::get_peer() - Got NULL pointer");
		return false;
	}

	if(myfd > 0){
		sw_socklen_t tmp = sizeof(sockaddr);
		if( getpeername(myfd, peer, &tmp) != 0 ){
			handle_errno(error, "SWBaseSocket::get_peer() error: ");
			return false;
		}
	}else{
		set_error(error, notConnected, "SWBaseSocket::get_peer() - No connection");
		return false;
	}
	
	no_error(error);
	return true;
}

void SWBaseSocket::reset()
{
	// Reset flags
	recv_close = false;	
	
	fsend_ready = true;
	frecv_ready = true;
}

bool SWBaseSocket::waitIO(io_type &type, SWBaseError *error)
{
	if( block_mode != blocking ){
		no_error(error);
		return true;
	}
	
	// We prefere to wait with select() even if no timeout is set
	// as select() behaves more predictable
	
	timeval t; 
	timeval *to = NULL;  // Indicate "wait for ever"
	t.tv_sec = tsec;
	t.tv_usec = tusec;
	 
	if( tsec > 0 || tusec > 0 )
		to = &t;
		
	fd_set readfds, writefds, exceptfds;
	
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);
	FD_SET(myfd, &readfds);
	FD_SET(myfd, &writefds);
	FD_SET(myfd, &exceptfds);
	
	int ret = 0;
	
	switch (type){
		case read:
			ret = select(myfd+1, &readfds, NULL, NULL, to);
			break;
		case write:
			ret = select(myfd+1, NULL, &writefds, NULL, to);
			break;
		case except:
			ret = select(myfd+1, NULL, NULL, &exceptfds, to);
			break;
		case rw:
			ret = select(myfd+1, &readfds, &writefds, NULL, to);
			break;
		case all:
			ret = select(myfd+1, &readfds, &writefds, &exceptfds, to);
			break;
	}
			
	if( ret < 0 ){
		handle_errno(error, "SWBaseSocket::waitIO() error: ");
		return false;
	}
	if( ret == 0 ){
		set_error(error, timeout, "SWBaseSocket::waitIO() timeout");
		return false;
	}

	if( FD_ISSET(myfd, &readfds) ){
		no_error(error);
		type = read;
		return true;
	}
	if( FD_ISSET(myfd, &writefds) ){
		no_error(error);
		type = write;
		return true;
	}
	if( FD_ISSET(myfd, &exceptfds) ){
		no_error(error);
		type = except;
		return true;
	}
	
	set_error(error, fatal, "SWBaseSocket::waitIO() failed on select()");
	return false;
}

bool SWBaseSocket::waitRead(SWBaseError *error)
{
	io_type tmp = read;
	return waitIO(tmp, error);
}

bool SWBaseSocket::waitWrite(SWBaseError *error)
{
	io_type tmp = write;
	return waitIO(tmp, error);
}

void SWBaseSocket::print_error()
{
	if( error_string.size() > 0 )
		fprintf(stderr, "%s!\n", error_string.c_str());
}

void SWBaseSocket::handle_errno(SWBaseError *error, string msg)
{
	#ifndef __WIN32__
	msg += strerror(errno);
	#else
	//stupid stupid stupid stupid M$
	switch (WSAGetLastError()){
		case 0:                   msg += "No error"; break;
		case WSAEINTR:            msg += "Interrupted system call"; break;
		case WSAEBADF:            msg += "Bad file number"; break;
		case WSAEACCES:           msg += "Permission denied"; break;
		case WSAEFAULT:           msg += "Bad address"; break;
		case WSAEINVAL:           msg += "Invalid argument"; break;
		case WSAEMFILE:           msg += "Too many open sockets"; break;
		case WSAEWOULDBLOCK:      msg += "Operation would block"; break;
		case WSAEINPROGRESS:      msg += "Operation now in progress"; break;
		case WSAEALREADY:         msg += "Operation already in progress"; break;
		case WSAENOTSOCK:         msg += "Socket operation on non-socket"; break;
		case WSAEDESTADDRREQ:     msg += "Destination address required"; break;
		case WSAEMSGSIZE:         msg += "Message too long"; break;
		case WSAEPROTOTYPE:       msg += "Protocol wrong type for socket"; break;
		case WSAENOPROTOOPT:      msg += "Bad protocol option"; break;
		case WSAEPROTONOSUPPORT:  msg += "Protocol not supported"; break;
		case WSAESOCKTNOSUPPORT:  msg += "Socket type not supported"; break;
		case WSAEOPNOTSUPP:       msg += "Operation not supported on socket"; break;
		case WSAEPFNOSUPPORT:     msg += "Protocol family not supported"; break;
		case WSAEAFNOSUPPORT:     msg += "Address family not supported"; break;
		case WSAEADDRINUSE:       msg += "Address already in use"; break;
		case WSAEADDRNOTAVAIL:    msg += "Can't assign requested address"; break;
		case WSAENETDOWN:         msg += "Network is down"; break;
		case WSAENETUNREACH:      msg += "Network is unreachable"; break;
		case WSAENETRESET:        msg += "Net connection reset"; break;
		case WSAECONNABORTED:     msg += "Software caused connection abort"; break;
		case WSAECONNRESET:       msg += "Connection reset by peer"; break;
		case WSAENOBUFS:          msg += "No buffer space available"; break;
		case WSAEISCONN:          msg += "Socket is already connected"; break;
		case WSAENOTCONN:         msg += "Socket is not connected"; break;
		case WSAESHUTDOWN:        msg += "Can't send after socket shutdown"; break;
		case WSAETOOMANYREFS:     msg += "Too many references"; break;
		case WSAETIMEDOUT:        msg += "Connection timed out"; break;
		case WSAECONNREFUSED:     msg += "Connection refused"; break;
		case WSAELOOP:            msg += "Too many levels of symbolic links"; break;
		case WSAENAMETOOLONG:     msg += "File name too long"; break;
		case WSAEHOSTDOWN:        msg += "Host is down"; break;
		case WSAEHOSTUNREACH:     msg += "No route to host"; break;
		case WSAENOTEMPTY:        msg += "Directory not empty"; break;
		case WSAEPROCLIM:         msg += "Too many processes"; break;
		case WSAEUSERS:           msg += "Too many users"; break;
		case WSAEDQUOT:           msg += "Disc quota exceeded"; break;
		case WSAESTALE:           msg += "Stale NFS file handle"; break;
		case WSAEREMOTE:          msg += "Too many levels of remote in path"; break;
		case WSASYSNOTREADY:      msg += "Network system is unavailable"; break;
		case WSAVERNOTSUPPORTED:  msg += "Winsock version out of range"; break;
		case WSANOTINITIALISED:   msg += "WSAStartup not yet called"; break;
		case WSAEDISCON:          msg += "Graceful shutdown in progress"; break;
		case WSAHOST_NOT_FOUND:   msg += "Host not found"; break;
		case WSANO_DATA:          msg += "No host data of that type was found"; break;
		default:                  msg += "Unknown Winsock error: " + WSAGetLastError(); break;
	}
	#endif
	
	int errorno;
	
	//Win32 braindamage
	#ifdef __WIN32__
	  errorno = WSAGetLastError();
	#else
	  errorno = errno;
	#endif
	
	SWBaseError e;
	
	if( errorno == EADDRINUSE )
		e = portInUse;
	else if( errorno == EAGAIN || errorno == EWOULDBLOCK )
		e = notReady;
	else if( errorno == EMSGSIZE )
		e = msgTooLong;
	else if( errorno == EINPROGRESS || errorno == EALREADY )
		e = notReady;
	else if( errorno == ECONNREFUSED || errorno == ETIMEDOUT )
		e = noResponse;
	else if( errorno == ENOTCONN || errorno == EBADF || errorno == ENOTSOCK )
		e = notConnected;
	else if( errorno == EPIPE ){
		e = terminated;
		recv_close = true;
	}else if( errorno == EINTR )
		e = interrupted;
	else
		e = fatal; //default
		
	set_error(error, e, msg);
}

void SWBaseSocket::no_error(SWBaseError *error)
{
	if(error != NULL){
		*error = ok;
		error->error_string = "";
		error->failed_class = NULL;
	}
}

void SWBaseSocket::set_error(SWBaseError *error, SWBaseError name, string msg)
{
	error_string = msg;

	if(error != NULL){
		*error = name;
		error->error_string = msg;
		error->failed_class = this;
	}else{
		if( sw_Verbose )
			print_error();
		
		if( sw_DoThrow ){
			SWBaseError e;
			e = name;
			e.error_string = msg;
			e.failed_class = this;
			throw e;
		}
	}
}

