../lsp/uncompressed.js: ../lsp/jquery.js ../lsp/placeholder.js ../lsp/md5.js ../lsp/main.js ../lsp/functions.js
	cat ../lsp/jquery.js ../lsp/placeholder.js ../lsp/md5.js ../lsp/main.js ../lsp/functions.js > ../lsp/uncompressed.js
../lsp/compressed.js: ../lsp/uncompressed.js
	echo "<script>" > ../lsp/compressed.js
	java -jar ../lsp/jscompiler.jar < ../lsp/uncompressed.js >> ../lsp/compressed.js
	echo "</script>" >> ../lsp/compressed.js
../server.html: ../lsp/compressed.js
	cat ../lsp/header.html ../lsp/compressed.js > ../server.html
	echo "<style>" >> ../server.html
	cat ../lsp/style.css >> ../server.html
	echo "</style>" >> ../server.html
	cat ../lsp/footer.html >> ../server.html
../server.html.h: ../server.html
	xxd -i ../server.html ../server.html.h
./embed.js.h: ./embed.js
	mv embed.js tmp.js
	java -jar ../lsp/jscompiler.jar < tmp.js > embed.js
	xxd -i embed.js embed.js.h
	mv tmp.js embed.js

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

