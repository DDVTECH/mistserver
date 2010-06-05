// C++ Socket Wrapper
// SocketW internal header 
//
// Started 030823
//
// License: LGPL v2.1+ (see the file LICENSE)
// (c)2002-2003 Anders Lindstr�m

/***********************************************************************
 *  This library is free software; you can redistribute it and/or      *
 *  modify it under the terms of the GNU Lesser General Public         *
 *  License as published by the Free Software Foundation; either       *
 *  version 2.1 of the License, or (at your option) any later version. *
 ***********************************************************************/
 
#ifndef sw_internal_H
#define sw_internal_H

// This header is included in all *.h files

#ifndef __WIN32__
  #include <sys/types.h> 
  #include <sys/socket.h> 
  #include <netinet/in.h>
  #include <sys/un.h>
#else
  #include <winsock2.h>
  
  #define F_SETFL FIONBIO
  #define O_NONBLOCK 1
#endif

#ifndef _SDL_H

// Define general types
typedef unsigned char  Uint8;
typedef signed char    Sint8;
typedef unsigned short Uint16;
typedef signed short   Sint16;
typedef unsigned int   Uint32;
typedef signed int     Sint32;

// It's VERY important that these types really have the right sizes!
// This black magic is from SDL
#define COMPILE_TIME_ASSERT(name, x)               \
       typedef int _dummy_ ## name[(x) * 2 - 1]
COMPILE_TIME_ASSERT(uint8, sizeof(Uint8) == 1);
COMPILE_TIME_ASSERT(sint8, sizeof(Sint8) == 1);
COMPILE_TIME_ASSERT(uint16, sizeof(Uint16) == 2);
COMPILE_TIME_ASSERT(sint16, sizeof(Sint16) == 2);
COMPILE_TIME_ASSERT(uint32, sizeof(Uint32) == 4);
COMPILE_TIME_ASSERT(sint32, sizeof(Sint32) == 4);
#undef COMPILE_TIME_ASSERT

#endif /* _SDL_H */

// Some compilers use a special export keyword
#ifndef DECLSPEC
  #ifdef __BEOS__
    #if defined(__GNUC__)
      #define DECLSPEC __declspec(dllexport)
    #else
      #define DECLSPEC __declspec(export)
    #endif
  #else
    #ifdef WIN32
      #define DECLSPEC __declspec(dllexport)
    #else
      #define DECLSPEC
    #endif
  #endif
#endif


#endif /* sw_internal_H */
