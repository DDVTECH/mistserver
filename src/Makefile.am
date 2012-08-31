lspdir=$(srcdir)/../lsp
CLOSUREJAR=$(lspdir)/closure-compiler.jar
CLOSURE=java -jar $(CLOSUREJAR) --warning_level QUIET
BUILT_SOURCES=server.html.h embed.js.h
# Do not clean below files, build machines do not have closure installed
EXTRA_DIST=server.html server.html.h embed.js.h

AM_CPPFLAGS = $(global_CFLAGS) $(MIST_CFLAGS)
LDADD = $(MIST_LIBS)
SUBDIRS=converters analysers
bin_PROGRAMS=MistBuffer MistController MistConnRAW MistConnRTMP MistConnHTTP MistConnHTTPProgressive MistConnHTTPDynamic MistPlayer
MistBuffer_SOURCES=buffer.cpp buffer_user.h buffer_user.cpp buffer_stream.h buffer_stream.cpp tinythread.cpp tinythread.h ../VERSION
MistBuffer_LDADD=$(MIST_LIBS) -lpthread
MistController_SOURCES=controller.cpp ../VERSION ./server.html.h
MistConnRAW_SOURCES=conn_raw.cpp ../VERSION
MistConnRTMP_SOURCES=conn_rtmp.cpp ../VERSION
MistConnHTTP_SOURCES=conn_http.cpp tinythread.cpp tinythread.h ../VERSION ./embed.js.h
MistConnHTTP_LDADD=$(MIST_LIBS) -lpthread
MistConnHTTPProgressive_SOURCES=conn_http_progressive.cpp ../VERSION
MistConnHTTPDynamic_SOURCES=conn_http_dynamic.cpp ../VERSION
MistPlayer_SOURCES=player.cpp
MistPlayer_LDADD=$(MIST_LIBS)

lspSOURCES=$(lspdir)/jquery.js $(lspdir)/placeholder.js $(lspdir)/md5.js $(lspdir)/main.js $(lspdir)/functions.js
lspDATA=$(lspdir)/header.html $(lspdir)/style.css $(lspdir)/footer.html

embed.js.h: $(srcdir)/embed.js
	$(CLOSURE) $< > embed.min.js
	xxd -i embed.min.js | sed s/_min_/_/g > embed.js.h
	rm embed.min.js

server.html: $(lspDATA)
	$(CLOSURE) $(lspSOURCES) > compressed.js
	cat $(lspdir)/header.html > $@
	echo "<script>" >> $@
	cat compressed.js >> $@
	rm compressed.js
	echo "</script><style>" >> $@
	cat $(lspdir)/style.css >> $@
	echo "</style>" >> $@
	cat $(lspdir)/footer.html >> $@

server.html.h: server.html
	xxd -i server.html server.html.h
