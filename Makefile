prefix = /usr
exec_prefix = $(prefix)
bindir = $(prefix)/bin

PACKAGE_VERSION := $(shell git describe --tags 2> /dev/null || cat VERSION 2> /dev/null || echo "Unknown")
DEBUG = 4
RELEASE = Generic_$(shell getconf LONG_BIT)

ifeq ($(PACKAGE_VERSION),Unknown)
  $(warning Version is unknown - consider creating a VERSION file or fixing your git setup.)
endif

CPPFLAGS = -Wall -funsigned-char -g -O2 -DDEBUG="$(DEBUG)" -DPACKAGE_VERSION="\"$(PACKAGE_VERSION)\"" -DRELEASE="\"$(RELEASE)\""

LDLIBS = -lmist


.DEFAULT_GOAL := all

all: controller buffers connectors analysers converters

DOXYGEN := $(shell doxygen -v 2> /dev/null)
ifdef DOXYGEN
all: docs
else
$(warning Doxygen not installed - not building source documentation.)
endif

controller: MistController
MistController: src/controller/server.html.h src/controller/*
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) src/controller/*.cpp

buffers: MistPlayer
MistPlayer: src/buffer/player.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

buffers: MistBuffer
MistBuffer: LDLIBS += -lpthread
MistBuffer: src/buffer/buffer.cpp src/buffer/buffer_stream.h src/buffer/buffer_stream.cpp tinythread.o tinythread.h
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) src/buffer/buffer.cpp src/buffer/buffer_stream.cpp tinythread.o

connectors: MistConnRaw
MistConnRaw: src/connectors/conn_raw.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

connectors: MistConnRTMP
MistConnRTMP: src/connectors/conn_rtmp.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

connectors: MistConnHTTP
MistConnHTTP: LDLIBS += -lpthread
MistConnHTTP: src/connectors/conn_http.cpp tinythread.o tinythread.h src/connectors/embed.js.h src/connectors/icon.h
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) src/connectors/conn_http.cpp tinythread.o

connectors: MistConnHTTPProgressiveFLV
MistConnHTTPProgressiveFLV: src/connectors/conn_http_progressive_flv.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

connectors: MistConnHTTPProgressiveMP3
MistConnHTTPProgressiveMP3: src/connectors/conn_http_progressive_mp3.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

connectors: MistConnHTTPProgressiveMP4
MistConnHTTPProgressiveMP4: src/connectors/conn_http_progressive_mp4.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

connectors: MistConnHTTPProgressiveOGG
MistConnHTTPProgressiveOGG: src/connectors/conn_http_progressive_ogg.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

connectors: MistConnHTTPDynamic
MistConnHTTPDynamic: src/connectors/conn_http_dynamic.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

connectors: MistConnHTTPSmooth
MistConnHTTPSmooth: src/connectors/conn_http_smooth.cpp src/connectors/xap.h
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) src/connectors/conn_http_smooth.cpp

connectors: MistConnHTTPLive
MistConnHTTPLive: src/connectors/conn_http_live.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

connectors: MistConnHTTPSRT 
MistConnHTTPSRT: src/connectors/conn_http_srt.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

connectors: MistConnHTTPJSON
MistConnHTTPJSON: src/connectors/conn_http_json.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

connectors: MistConnTS
MistConnTS: src/connectors/conn_ts.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

analysers: MistAnalyserRTMP
MistAnalyserRTMP: src/analysers/rtmp_analyser.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

analysers: MistAnalyserFLV
MistAnalyserFLV: src/analysers/flv_analyser.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

analysers: MistAnalyserDTSC
MistAnalyserDTSC: src/analysers/dtsc_analyser.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

analysers: MistAnalyserAMF
MistAnalyserAMF: src/analysers/amf_analyser.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

analysers: MistAnalyserMP4
MistAnalyserMP4: src/analysers/mp4_analyser.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

analysers: MistAnalyserOGG
MistAnalyserOGG: src/analysers/ogg_analyser.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

analysers: MistInfo
MistInfo: src/analysers/info.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

converters: MistDTSC2FLV
MistDTSC2FLV: src/converters/dtsc2flv.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

converters: MistFLV2DTSC
MistFLV2DTSC: src/converters/flv2dtsc.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

converters: MistOGG2DTSC
MistOGG2DTSC: src/converters/ogg2dtsc.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

converters: MistDTSC2OGG
MistDTSC2OGG: src/converters/dtsc2ogg.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

converters: MistDTSCFix
MistDTSCFix: src/converters/dtscfix.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

converters: MistDTSCMerge
MistDTSCMerge: src/converters/dtscmerge.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

converters: MistDTSC2TS
MistDTSC2TS: src/converters/dtsc2ts.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

converters: MistSRT2DTSC
MistSRT2DTSC: src/converters/srt2dtsc.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

converters: MistDTSC2SRT
MistDTSC2SRT: src/converters/dtsc2srt.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

converters: MistDTSC2MP4
MistDTSC2MP4: src/converters/dtsc2mp4.cpp
	$(CXX) -o $@ $(LDLIBS) $(CPPFLAGS) $^

BUILT_SOURCES=controller/server.html.h connectors/embed.js.h
lspSOURCES=lsp/jquery.js lsp/placeholder.js lsp/md5.js lsp/main.js lsp/pages.js lsp/tablesort.js
lspDATA=lsp/header.html lsp/main.css lsp/footer.html

JAVA := $(shell which java 2> /dev/null)
ifdef JAVA
CLOSURE=java -jar lsp/closure-compiler.jar --warning_level QUIET
else
$(warning Java not installed - not compressing javascript codes before inclusion.)
CLOSURE=cat
endif

XXD := $(shell which xxd 2> /dev/null)
ifndef XXD
$(error xxd not installed - cannot continue. Please install xxd)
endif

src/connectors/embed.js.h: src/connectors/embed.js
	$(CLOSURE) src/connectors/embed.js > embed.min.js
	xxd -i embed.min.js | sed s/_min_/_/g > src/connectors/embed.js.h
	rm embed.min.js

src/controller/server.html: $(lspDATA) $(lspSOURCES)
	cat lsp/header.html > $@
	echo "<script>" >> $@
	$(CLOSURE) $(lspSOURCES) >> $@
	echo "</script><style>" >> $@
	cat lsp/main.css >> $@
	echo "</style>" >> $@
	cat lsp/footer.html >> $@

src/controller/server.html.h: src/controller/server.html
	cd src/controller; xxd -i server.html server.html.h

docs: src/* Doxyfile
	doxygen ./Doxyfile > /dev/null

clean:
	rm -f *.o Mist* src/controller/server.html src/connectors/embed.js.h src/controller/server.html.h
	rm -rf ./docs

install: controller buffers connectors analysers converters
	install ./Mist* $(DESTDIR)$(bindir)

uninstall:
	rm -f $(DESTDIR)$(bindir)/Mist*

.PHONY: clean uninstall

