AM_CPPFLAGS = $(MIST_CFLAGS)
LDADD = $(MIST_LIBS)
SUBDIRS=converters analysers
bin_PROGRAMS=MistBuffer MistController MistConnRAW MistConnRTMP MistConnHTTP MistConnHTTPProgressive MistConnHTTPDynamic
MistBuffer_SOURCES=buffer.cpp buffer_user.h buffer_user.cpp buffer_stream.h buffer_stream.cpp tinythread.cpp tinythread.h ../VERSION
MistBuffer_LDADD=$(MIST_LIBS) -lpthread
MistController_SOURCES=controller.cpp ../VERSION ../server.html.h
MistConnRAW_SOURCES=conn_raw.cpp ../VERSION
MistConnRTMP_SOURCES=conn_rtmp.cpp ../VERSION
MistConnHTTP_SOURCES=conn_http.cpp tinythread.cpp tinythread.h ../VERSION ./embed.js.h
MistConnHTTP_LDADD=$(MIST_LIBS) -lpthread
MistConnHTTPProgressive_SOURCES=conn_http_progressive.cpp ../VERSION
MistConnHTTPDynamic_SOURCES=conn_http_dynamic.cpp ../VERSION

