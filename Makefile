prefix = /usr
exec_prefix = $(prefix)
bindir = $(prefix)/bin

PACKAGE_VERSION := $(shell git describe --tags 2> /dev/null || cat VERSION 2> /dev/null || echo "Unknown")
DEBUG = 4
RELEASE = Generic_$(shell getconf LONG_BIT)

ifeq ($(PACKAGE_VERSION),Unknown)
  $(warning Version is unknown - consider creating a VERSION file or fixing your git setup.)
endif

CPPFLAGS = -Wall -g -O2
override CPPFLAGS += -funsigned-char -DDEBUG="$(DEBUG)" -DPACKAGE_VERSION="\"$(PACKAGE_VERSION)\"" -DRELEASE="\"$(RELEASE)\""

ifdef WITH_THREADNAMES
override CPPFLAGS += -DWITH_THREADNAMES=1
endif

THREADLIB = -lpthread
LDLIBS = -lmist -lrt


.DEFAULT_GOAL := all

all: MistConnHTTP controller analysers inputs outputs

DOXYGEN := $(shell doxygen -v 2> /dev/null)
ifdef DOXYGEN
all: docs
else
$(warning Doxygen not installed - not building source documentation.)
endif

controller: MistController
MistController: override LDLIBS += $(THREADLIB)
MistController: src/controller/server.html.h src/controller/*
	$(CXX) $(LDFLAGS) $(CPPFLAGS) src/controller/*.cpp $(LDLIBS) -o $@

connectors: MistConnHTTP
MistConnHTTP: override LDLIBS += $(THREADLIB)
MistConnHTTP: src/connectors/conn_http.cpp src/connectors/embed.js.h src/connectors/icon.h
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $< $(LDLIBS) -o $@

analysers: MistAnalyserRTMP
MistAnalyserRTMP: src/analysers/rtmp_analyser.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

analysers: MistAnalyserFLV
MistAnalyserFLV: src/analysers/flv_analyser.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

analysers: MistAnalyserDTSC
MistAnalyserDTSC: src/analysers/dtsc_analyser.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

analysers: MistAnalyserAMF
MistAnalyserAMF: src/analysers/amf_analyser.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

analysers: MistAnalyserMP4
MistAnalyserMP4: src/analysers/mp4_analyser.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

analysers: MistAnalyserOGG
MistAnalyserOGG: src/analysers/ogg_analyser.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

analysers: MistInfo
MistInfo: src/analysers/info.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

converters: MistDTSC2FLV
MistDTSC2FLV: src/converters/dtsc2flv.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

converters: MistFLV2DTSC
MistFLV2DTSC: src/converters/flv2dtsc.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

converters: MistDTSCFix
MistDTSCFix: src/converters/dtscfix.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

converters: MistDTSCMerge
MistDTSCMerge: src/converters/dtscmerge.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

converters: MistDTSC2TS
MistDTSC2TS: src/converters/dtsc2ts.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

converters: MistSRT2DTSC
MistSRT2DTSC: src/converters/srt2dtsc.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

converters: MistDTSC2SRT
MistDTSC2SRT: src/converters/dtsc2srt.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

inputs: MistInDTSC
MistInDTSC: override LDLIBS += $(THREADLIB)
MistInDTSC: override CPPFLAGS += "-DINPUTTYPE=\"input_dtsc.h\""
MistInDTSC: src/input/mist_in.cpp src/input/input.cpp src/input/input_dtsc.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

inputs: MistInFLV
MistInFLV: override LDLIBS += $(THREADLIB)
MistInFLV: override CPPFLAGS += "-DINPUTTYPE=\"input_flv.h\""
MistInFLV: src/input/mist_in.cpp src/input/input.cpp src/input/input_flv.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

inputs: MistInOGG
MistInOGG: override LDLIBS += $(THREADLIB)
MistInOGG: override CPPFLAGS += "-DINPUTTYPE=\"input_ogg.h\""
MistInOGG: src/input/mist_in.cpp src/input/input.cpp src/input/input_ogg.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

inputs: MistInBuffer
MistInBuffer: override LDLIBS += $(THREADLIB)
MistInBuffer: override CPPFLAGS += "-DINPUTTYPE=\"input_buffer.h\""
MistInBuffer: src/input/mist_in.cpp src/input/input.cpp src/input/input_buffer.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutFLV
MistOutFLV: override LDLIBS += $(THREADLIB)
MistOutFLV: override CPPFLAGS += "-DOUTPUTTYPE=\"output_progressive_flv.h\""
MistOutFLV: src/output/mist_out_http.cpp src/output/output.cpp src/output/output_progressive_flv.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutMP4
MistOutMP4: override LDLIBS += $(THREADLIB)
MistOutMP4: override CPPFLAGS += "-DOUTPUTTYPE=\"output_progressive_mp4.h\""
MistOutMP4: src/output/mist_out_http.cpp src/output/output.cpp src/output/output_progressive_mp4.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutMP3
MistOutMP3: override LDLIBS += $(THREADLIB)
MistOutMP3: override CPPFLAGS += "-DOUTPUTTYPE=\"output_progressive_mp3.h\""
MistOutMP3: src/output/mist_out_http.cpp src/output/output.cpp src/output/output_progressive_mp3.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutRTMP
MistOutRTMP: override LDLIBS += $(THREADLIB)
MistOutRTMP: override CPPFLAGS += "-DOUTPUTTYPE=\"output_rtmp.h\""
MistOutRTMP: src/output/mist_out.cpp src/output/output.cpp src/output/output_rtmp.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutRaw
MistOutRaw: override LDLIBS += $(THREADLIB)
MistOutRaw: override CPPFLAGS += "-DOUTPUTTYPE=\"output_raw.h\""
MistOutRaw: src/output/mist_out.cpp src/output/output.cpp src/output/output_raw.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutTS
MistOutTS: override LDLIBS += $(THREADLIB)
MistOutTS: override CPPFLAGS += "-DOUTPUTTYPE=\"output_ts.h\""
MistOutTS: src/output/mist_out.cpp src/output/output.cpp src/output/output_ts.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutHSS
MistOutHSS: override LDLIBS += $(THREADLIB)
MistOutHSS: override CPPFLAGS += "-DOUTPUTTYPE=\"output_hss.h\""
MistOutHSS: src/output/mist_out_http.cpp src/output/output.cpp src/output/output_hss.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@
	
outputs: MistOutHLS
MistOutHLS: override LDLIBS += $(THREADLIB)
MistOutHLS: override CPPFLAGS += "-DOUTPUTTYPE=\"output_hls.h\""
MistOutHLS: src/output/mist_out_http.cpp src/output/output.cpp src/output/output_hls.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@
	
outputs: MistOutHDS
MistOutHDS: override LDLIBS += $(THREADLIB)
MistOutHDS: override CPPFLAGS += "-DOUTPUTTYPE=\"output_hds.h\""
MistOutHDS: src/output/mist_out_http.cpp src/output/output.cpp src/output/output_hds.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutSRT
MistOutSRT: override LDLIBS += $(THREADLIB)
MistOutSRT: override CPPFLAGS += "-DOUTPUTTYPE=\"output_srt.h\""
MistOutSRT: src/output/mist_out_http.cpp src/output/output.cpp src/output/output_srt.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@
	
outputs: MistOutJSON
MistOutJSON: override LDLIBS += $(THREADLIB)
MistOutJSON: override CPPFLAGS += "-DOUTPUTTYPE=\"output_json.h\""
MistOutJSON: src/output/mist_out_http.cpp src/output/output.cpp src/output/output_json.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

BUILT_SOURCES=controller/server.html.h connectors/embed.js.h
lspSOURCES=lsp/plugins/jquery.js lsp/plugins/placeholder.js lsp/plugins/md5.js lsp/main.js lsp/pages.js lsp/plugins/tablesort.js lsp/plugins/jquery.flot.min.js lsp/plugins/jquery.flot.time.min.js lsp/plugins/jquery.flot.crosshair.min.js
lspDATA=lsp/header.html lsp/main.css lsp/footer.html

JAVA := $(shell which java 2> /dev/null)
ifdef JAVA
CLOSURE=java -jar lsp/closure-compiler.jar --warning_level QUIET
else
$(warning Java not installed - not compressing javascript codes before inclusion.)
CLOSURE=cat
endif

sourcery: src/sourcery.cpp
	$(CXX) -o $@ $(CPPFLAGS) $^

src/connectors/embed.js.h: src/connectors/embed.js sourcery
	$(CLOSURE) src/connectors/embed.js > embed.min.js
	./sourcery embed.min.js embed_js > src/connectors/embed.js.h
	rm embed.min.js

src/controller/server.html: $(lspDATA) $(lspSOURCES)
	cat lsp/header.html > $@
	echo "<script>" >> $@
	$(CLOSURE) $(lspSOURCES) >> $@
	echo "</script><style>" >> $@
	cat lsp/main.css >> $@
	echo "</style>" >> $@
	cat lsp/footer.html >> $@

src/controller/server.html.h: src/controller/server.html sourcery
	cd src/controller; ../../sourcery server.html server_html > server.html.h

docs: src/* Doxyfile
	doxygen ./Doxyfile > /dev/null

clean:
	rm -f *.o Mist* sourcery src/controller/server.html src/connectors/embed.js.h src/controller/server.html.h
	rm -rf ./docs

distclean: clean

install: all
	mkdir -p $(DESTDIR)$(bindir)
	install -m 755 ./Mist* $(DESTDIR)$(bindir)

uninstall:
	rm -f $(DESTDIR)$(bindir)/Mist*

.PHONY: clean uninstall

