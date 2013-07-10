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
bin_PROGRAMS=MistBuffer 
bin_PROGRAMS+=MistController 
bin_PROGRAMS+=MistConnRAW 
bin_PROGRAMS+=MistConnRTMP
bin_PROGRAMS+=MistConnHTTP 
bin_PROGRAMS+=MistConnHTTPProgressiveFLV 
bin_PROGRAMS+=MistConnHTTPProgressiveMP3 
bin_PROGRAMS+=MistConnHTTPProgressiveMP4 
bin_PROGRAMS+=MistConnHTTPProgressiveOGG 
bin_PROGRAMS+=MistConnHTTPDynamic 
bin_PROGRAMS+=MistConnHTTPSmooth 
bin_PROGRAMS+=MistConnHTTPLive 
bin_PROGRAMS+=MistConnTS 
bin_PROGRAMS+=MistPlayer 
bin_PROGRAMS+=MistDTSC2FLV
bin_PROGRAMS+=MistFLV2DTSC
bin_PROGRAMS+=MistDTSCFix 
bin_PROGRAMS+=MistDTSCMerge 
bin_PROGRAMS+=MistDTSC2TS 
bin_PROGRAMS+=MistDTSC2OGG 
bin_PROGRAMS+=MistDTSC2MP4
bin_PROGRAMS+=MistSRT2DTSC 
bin_PROGRAMS+=MistOGG2DTSC 
bin_PROGRAMS+=MistAnalyserRTMP 
bin_PROGRAMS+=MistAnalyserFLV 
bin_PROGRAMS+=MistAnalyserOGG 
bin_PROGRAMS+=MistAnalyserDTSC 
bin_PROGRAMS+=MistAnalyserAMF 
bin_PROGRAMS+=MistAnalyserMP4 
bin_PROGRAMS+=MistInfo
bin_PROGRAMS+=MistDTSC2MP4
bin_PROGRAMS+=MistDTSC2SRT

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
MistConnHTTPProgressiveFLV_SOURCES=connectors/conn_http_progressive_flv.cpp ../VERSION
MistConnHTTPProgressiveMP3_SOURCES=connectors/conn_http_progressive_mp3.cpp ../VERSION
MistConnHTTPProgressiveMP4_SOURCES=connectors/conn_http_progressive_mp4.cpp ../VERSION
MistConnHTTPProgressiveOGG_SOURCES=connectors/conn_http_progressive_ogg.cpp ../VERSION
MistConnHTTPDynamic_SOURCES=connectors/conn_http_dynamic.cpp ../VERSION
MistConnHTTPSmooth_SOURCES=connectors/conn_http_smooth.cpp ../VERSION
MistConnHTTPLive_SOURCES=connectors/conn_http_live.cpp ../VERSION
MistConnTS_SOURCES=connectors/conn_ts.cpp ../VERSION

#converters directory (Mist*2*)
MistDTSC2FLV_SOURCES=converters/dtsc2flv.cpp
MistFLV2DTSC_SOURCES=converters/flv2dtsc.cpp
MistOGG2DTSC_SOURCES=converters/ogg2dtsc.cpp
MistDTSC2OGG_SOURCES=converters/dtsc2ogg.cpp
MistDTSCFix_SOURCES=converters/dtscfix.cpp
MistDTSCMerge_SOURCES=converters/dtscmerge.cpp
MistDTSC2TS_SOURCES=converters/dtsc2ts.cpp
MistSRT2DTSC_SOURCES=converters/srt2dtsc.cpp
MistDTSC2MP4_SOURCES=converters/dtsc2mp4.cpp
MistDTSC2SRT_SOURCES=converters/dtsc2srt.cpp

#analysers directory (MistAnalyser*)
MistAnalyserRTMP_SOURCES=analysers/rtmp_analyser.cpp
MistAnalyserFLV_SOURCES=analysers/flv_analyser.cpp
MistAnalyserDTSC_SOURCES=analysers/dtsc_analyser.cpp
MistAnalyserAMF_SOURCES=analysers/amf_analyser.cpp
MistAnalyserMP4_SOURCES=analysers/mp4_analyser.cpp
MistAnalyserOGG_SOURCES=analysers/ogg_analyser.cpp
MistInfo_SOURCES=info.cpp


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

