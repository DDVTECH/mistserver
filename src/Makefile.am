CLOSUREJAR=../lsp/closure-compiler.jar
CLOSURE=java -jar $(CLOSUREJAR) --warning_level QUIET
BUILT_SOURCES=controller/server.html.h connectors/embed.js.h
lspSOURCES=../lsp/jquery.js ../lsp/placeholder.js ../lsp/md5.js ../lsp/main.js ../lsp/functions.js ../lsp/tablesort.js
lspDATA=../lsp/header.html ../lsp/style.css ../lsp/footer.html

# Do not clean below files, build machines do not have closure installed
EXTRA_DIST=controller/server.html controller/server.html.h connectors/embed.js connectors/embed.js.h $(lspSOURCES) $(lspDATA)

RELEASE ?= "Generic_`getconf LONG_BIT`"

AM_CPPFLAGS = $(global_CFLAGS) $(MIST_CFLAGS) -DRELEASE=\"$(RELEASE)\"
LDADD = $(MIST_LIBS)
bin_PROGRAMS=MistBuffer MistController MistConnRAW MistConnRTMP MistConnHTTP MistConnHTTPProgressive MistConnHTTPDynamic MistConnHTTPSmooth MistConnHTTPLive MistConnTS MistPlayer MistDTSC2FLV MistFLV2DTSC MistDTSCFix MistDTSC2TS MistAnalyserRTMP MistAnalyserFLV MistAnalyserDTSC MistAnalyserAMF MistAnalyserMP4

#buffer folder (MistBuffer, MistPlayer)
MistBuffer_SOURCES=buffer/buffer.cpp buffer/buffer_user.h buffer/buffer_user.cpp buffer/buffer_stream.h buffer/buffer_stream.cpp tinythread.cpp tinythread.h ../VERSION
MistBuffer_LDADD=$(MIST_LIBS) -lpthread
MistPlayer_SOURCES=buffer/player.cpp

#controller folder (MistController)
MistController_SOURCES=controller/controller.cpp controller/controller_connectors.h controller/controller_connectors.cpp controller/controller_storage.h controller/controller_storage.cpp controller/controller_streams.h controller/controller_streams.cpp controller/controller_capabilities.h controller/controller_capabilities.cpp ../VERSION controller/server.html.h

#connectors folder (MistConn*)
MistConnRAW_SOURCES=connectors/conn_raw.cpp ../VERSION
MistConnRTMP_SOURCES=connectors/conn_rtmp.cpp ../VERSION
MistConnHTTP_SOURCES=connectors/conn_http.cpp tinythread.cpp tinythread.h ../VERSION connectors/embed.js.h connectors/icon.h
MistConnHTTP_LDADD=$(MIST_LIBS) -lpthread
MistConnHTTPProgressive_SOURCES=connectors/conn_http_progressive.cpp ../VERSION
MistConnHTTPDynamic_SOURCES=connectors/conn_http_dynamic.cpp ../VERSION
MistConnHTTPSmooth_SOURCES=connectors/conn_http_smooth.cpp ../VERSION
MistConnHTTPLive_SOURCES=connectors/conn_http_live.cpp ../VERSION
MistConnTS_SOURCES=connectors/conn_ts.cpp ../VERSION

#converters directory (Mist*2*)
MistDTSC2FLV_SOURCES=converters/dtsc2flv.cpp
MistFLV2DTSC_SOURCES=converters/flv2dtsc.cpp
MistDTSCFix_SOURCES=converters/dtscfix.cpp
MistDTSC2TS_SOURCES=converters/dtsc2ts.cpp

#analysers directory (MistAnalyser*)
MistAnalyserRTMP_SOURCES=analysers/rtmp_analyser.cpp
MistAnalyserFLV_SOURCES=analysers/flv_analyser.cpp
MistAnalyserDTSC_SOURCES=analysers/dtsc_analyser.cpp
MistAnalyserAMF_SOURCES=analysers/amf_analyser.cpp
MistAnalyserMP4_SOURCES=analysers/mp4_analyser.cpp


connectors/embed.js.h: connectors/embed.js
	$(CLOSURE) $(srcdir)/connectors/embed.js > embed.min.js
	xxd -i embed.min.js | sed s/_min_/_/g > $(srcdir)/connectors/embed.js.h
	rm embed.min.js

controller/server.html: $(lspDATA) $(lspSOURCES)
	$(CLOSURE) $(lspSOURCES) > compressed.js
	cat ../lsp/header.html > $@
	echo "<script>" >> $@
	cat compressed.js >> $@
	rm compressed.js
	echo "</script><style>" >> $@
	cat ../lsp/style.css >> $@
	echo "</style>" >> $@
	cat ../lsp/footer.html >> $@

controller/server.html.h: controller/server.html
	cd $(srcdir)/controller; xxd -i server.html server.html.h

