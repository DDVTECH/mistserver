CLOSUREJAR=../lsp/closure-compiler.jar
CLOSURE=java -jar $(CLOSUREJAR) --warning_level QUIET
BUILT_SOURCES=server.html.h embed.js.h
lspSOURCES=../lsp/jquery.js ../lsp/placeholder.js ../lsp/md5.js ../lsp/main.js ../lsp/functions.js ../lsp/tablesort.js
lspDATA=../lsp/header.html ../lsp/style.css ../lsp/footer.html

# Do not clean below files, build machines do not have closure installed
EXTRA_DIST=server.html server.html.h embed.js embed.js.h $(lspSOURCES) $(lspDATA)

RELEASE ?= "Generic_`getconf LONG_BIT`"

AM_CPPFLAGS = $(global_CFLAGS) $(MIST_CFLAGS) -DRELEASE=\"$(RELEASE)\"
LDADD = $(MIST_LIBS)
SUBDIRS=converters analysers
bin_PROGRAMS=MistBuffer MistController MistConnRAW MistConnRTMP MistConnHTTP MistConnHTTPProgressive MistConnHTTPDynamic MistConnHTTPSmooth MistConnHTTPLive MistConnTS MistPlayer
MistBuffer_SOURCES=buffer.cpp buffer_user.h buffer_user.cpp buffer_stream.h buffer_stream.cpp tinythread.cpp tinythread.h ../VERSION
MistBuffer_LDADD=$(MIST_LIBS) -lpthread
MistController_SOURCES=controller.cpp controller_connectors.h controller_connectors.cpp controller_storage.h controller_storage.cpp controller_streams.h controller_streams.cpp controller_capabilities.h controller_capabilities.cpp ../VERSION ./server.html.h
MistConnRAW_SOURCES=conn_raw.cpp ../VERSION
MistConnRTMP_SOURCES=conn_rtmp.cpp ../VERSION
MistConnHTTP_SOURCES=conn_http.cpp tinythread.cpp tinythread.h ../VERSION ./embed.js.h
MistConnHTTP_LDADD=$(MIST_LIBS) -lpthread
MistConnHTTPProgressive_SOURCES=conn_http_progressive.cpp ../VERSION
MistConnHTTPDynamic_SOURCES=conn_http_dynamic.cpp ../VERSION
MistConnHTTPSmooth_SOURCES=conn_http_smooth.cpp ../VERSION
MistConnHTTPLive_SOURCES=conn_http_live.cpp ../VERSION
MistConnTS_SOURCES=conn_ts.cpp ../VERSION
MistPlayer_SOURCES=player.cpp
MistPlayer_LDADD=$(MIST_LIBS)


embed.js.h: $(srcdir)/embed.js
	$(CLOSURE) $< > embed.min.js
	xxd -i embed.min.js | sed s/_min_/_/g > embed.js.h
	rm embed.min.js

server.html: $(lspDATA) $(lspSOURCES)
	$(CLOSURE) $(lspSOURCES) > compressed.js
	cat ../lsp/header.html > $@
	echo "<script>" >> $@
	cat compressed.js >> $@
	rm compressed.js
	echo "</script><style>" >> $@
	cat ../lsp/style.css >> $@
	echo "</style>" >> $@
	cat ../lsp/footer.html >> $@

server.html.h: server.html
	xxd -i server.html server.html.h
