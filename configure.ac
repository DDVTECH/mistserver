#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.60])
AC_INIT([MistServer],
	m4_esyscmd([git rev-parse 2>/dev/null&&git describe --tags > VERSION;tr -d '\n' < VERSION]),
	[contact@ddvtech.com])
AC_CONFIG_SRCDIR([src/tinythread.cpp])
# foreign: no need for NEWS or INSTALL files
AM_INIT_AUTOMAKE([foreign subdir-objects])
LT_INIT

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

# Checks for libraries.
AC_DEFINE(_GNU_SOURCE)
#AC_CHECK_LIB(ssl, RC4)
PKG_CHECK_MODULES([MIST], [mist-1.0 >= 5.1.2])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])


# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([dup2 gettimeofday memset mkdir socket strerror])

AC_SUBST([global_CFLAGS], [-funsigned-char])

#allow verbose mode compiles
AC_ARG_ENABLE([verbose], AC_HELP_STRING([--enable-verbose], [Compile with verbose messages]),
	AC_DEFINE([DEBUG], [4]))

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
