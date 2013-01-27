AM_CPPFLAGS = $(global_CFLAGS)

lib_LTLIBRARIES=libmist-1.0.la
libmist_1_0_la_SOURCES=amf.h amf.cpp auth.h auth.cpp base64.h base64.cpp config.h config.cpp dtsc.h dtsc.cpp flv_tag.h flv_tag.cpp http_parser.h http_parser.cpp json.h json.cpp procs.h procs.cpp rtmpchunks.h rtmpchunks.cpp socket.h socket.cpp mp4.h mp4.cpp ftp.h ftp.cpp filesystem.h filesystem.cpp stream.h stream.cpp timing.h timing.cpp ts_packet.cpp ts_packet.h
libmist_1_0_la_LIBADD=-lssl -lcrypto -lrt
libmist_1_0_la_LDFLAGS = -version-info 4:0:1

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = mist-1.0.pc

library_includedir=$(includedir)/mist-1.0/mist
library_include_HEADERS = amf.h auth.h base64.h config.h dtsc.h flv_tag.h http_parser.h json.h procs.h rtmpchunks.h socket.h mp4.h ftp.h filesystem.h stream.h timing.h nal.h ts_packet.h
