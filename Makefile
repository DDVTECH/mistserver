prefix = /usr
exec_prefix = $(prefix)
bindir = $(prefix)/bin

PACKAGE_VERSION := $(shell git describe --tags 2> /dev/null || cat VERSION 2> /dev/null || echo "Unknown")
DEBUG = 4
RELEASE = Generic_$(shell getconf LONG_BIT)
GEOIP = -lGeoIP # /*LTS*/

ifeq ($(PACKAGE_VERSION),Unknown)
  $(warning Version is unknown - consider creating a VERSION file or fixing your git setup.)
endif

CPPFLAGS = -Wall -g -O2
override CPPFLAGS += -funsigned-char -DDEBUG="$(DEBUG)" -DPACKAGE_VERSION="\"$(PACKAGE_VERSION)\"" -DRELEASE="\"$(RELEASE)\""

ifdef GEOIP
override CPPFLAGS += -DGEOIP=1
endif

ifndef NOUPDATE
override CPPFLAGS += -DUPDATER=1
endif

ifdef NOAUTH
override CPPFLAGS += -DNOAUTH=1
endif

ifdef KILLONEXIT
override CPPFLAGS += -DKILL_ON_EXIT=true
endif

ifdef WITH_THREADNAMES
override CPPFLAGS += -DWITH_THREADNAMES=1
endif

# /*LTS-START*/
FILLER_DATA =
SHARED_SECRET =
SUPER_SECRET =
override CPPFLAGS += -DFILLER_DATA=\"$(FILLER_DATA)\" -DSHARED_SECRET=\"$(SHARED_SECRET)\" -DSUPER_SECRET=\"$(SUPER_SECRET)\"
ifeq ($(FILLER_DATA),)
  $(warning Filler data is empty and this is an LTS build - did you set FILLER_DATA?)
endif
ifeq ($(SHARED_SECRET),)
  $(warning Shared secret is empty and this is an LTS build - did you set SHARED_SECRET?)
endif
ifeq ($(SUPER_SECRET),)
  $(warning Super secret is empty and this is an LTS build - did you set SUPER_SECRET?)
endif
# /*LTS-END*/


THREADLIB = -lpthread
LDLIBS = -lmist -lrt


.DEFAULT_GOAL := all

all: controller analysers inputs outputs

DOXYGEN := $(shell doxygen -v 2> /dev/null)
ifdef DOXYGEN
all: docs
else
$(warning Doxygen not installed - not building source documentation.)
endif

controller: MistController
MistController: override LDLIBS += $(THREADLIB)
MistController: override LDLIBS += $(GEOIP) # /*LTS*/
MistController: src/controller/server.html.h src/controller/*
	$(CXX) $(LDFLAGS) $(CPPFLAGS) src/controller/*.cpp $(LDLIBS) -o $@

analysers: MistAnalyserRTMP
MistAnalyserRTMP: src/analysers/rtmp_analyser.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@
	
analysers: MistAnalyserRTP
MistAnalyserRTP: src/analysers/rtp_analyser.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

analysers: MistAnalyserTS
MistAnalyserTS: src/analysers/ts_analyser.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

analysers: MistAnalyserRTSP
MistAnalyserRTSP: src/analysers/rtsp_rtp_analyser.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@
	
analysers: MistAnalyserStats
MistAnalyserStats: src/analysers/stats_analyser.cpp
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

#analysers: MistAnalyserOGG
#MistAnalyserOGG: src/analysers/ogg_analyser.cpp
#	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

analysers: MistInfo
MistInfo: src/analysers/info.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

inputs: MistInDTSC
MistInDTSC: override LDLIBS += $(THREADLIB)
MistInDTSC: override CPPFLAGS += "-DINPUTTYPE=\"input_dtsc.h\""
MistInDTSC: src/input/mist_in.cpp src/input/input.cpp src/input/input_dtsc.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

inputs: MistInISMV
MistInISMV: override LDLIBS += $(THREADLIB)
MistInISMV: override CPPFLAGS += "-DINPUTTYPE=\"input_ismv.h\""
MistInISMV: src/input/mist_in.cpp src/input/input.cpp src/input/input_ismv.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

inputs: MistInFLV
MistInFLV: override LDLIBS += $(THREADLIB)
MistInFLV: override CPPFLAGS += "-DINPUTTYPE=\"input_flv.h\""
MistInFLV: src/input/mist_in.cpp src/input/input.cpp src/input/input_flv.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

inputs: MistInTS
MistInTS: override LDLIBS += $(THREADLIB)
MistInTS: override CPPFLAGS += "-DINPUTTYPE=\"input_ts.h\""
MistInTS: src/input/mist_in.cpp src/input/input.cpp src/input/input_ts.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

inputs: MistInMP4
MistInMP4: override LDLIBS += $(THREADLIB)
MistInMP4: override CPPFLAGS += "-DINPUTTYPE=\"input_mp4.h\""
MistInMP4: src/input/mist_in.cpp src/input/input.cpp src/input/input_mp4.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

#inputs: MistInOGG
#MistInOGG: override LDLIBS += $(THREADLIB)
#MistInOGG: override CPPFLAGS += "-DINPUTTYPE=\"input_ogg.h\""
#MistInOGG: src/input/mist_in.cpp src/input/input.cpp src/input/input_ogg.cpp
#	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

inputs: MistInBuffer
MistInBuffer: override LDLIBS += $(THREADLIB)
MistInBuffer: override CPPFLAGS += "-DINPUTTYPE=\"input_buffer.h\""
MistInBuffer: src/input/mist_in.cpp src/input/input.cpp src/input/input_buffer.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

inputs: MistInFolder
MistInFolder: override LDLIBS += $(THREADLIB)
MistInFolder: override CPPFLAGS += "-DINPUTTYPE=\"input_folder.h\""
MistInFolder: src/input/mist_in_folder.cpp src/input/input.cpp src/input/input_folder.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

MistInAV: override LDLIBS += -lavformat -lavcodec -lavutil $(THREADLIB)
MistInAV: override CPPFLAGS += "-DINPUTTYPE=\"input_av.h\""
MistInAV: src/input/mist_in.cpp src/input/input.cpp src/input/input_av.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) -D__STDC_CONSTANT_MACROS=1 $^ $(LDLIBS) -o $@

outputs: MistOutFLV
MistOutFLV: override LDLIBS += $(THREADLIB)
MistOutFLV: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutFLV: override CPPFLAGS += "-DOUTPUTTYPE=\"output_progressive_flv.h\""
MistOutFLV: src/output/mist_out.cpp src/output/output_http.cpp src/output/output.cpp src/output/output_progressive_flv.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@
	
# /*LTS-START*/
outputs: MistOutRTSP
MistOutRTSP: override LDLIBS += $(THREADLIB)
MistOutRTSP: override LDLIBS += $(GEOIP)
MistOutRTSP: override CPPFLAGS += "-DOUTPUTTYPE=\"output_rtsp.h\""
MistOutRTSP: src/output/mist_out.cpp src/output/output.cpp src/output/output_rtsp.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@
# /*LTS-END*/

outputs: MistOutMP4
MistOutMP4: override LDLIBS += $(THREADLIB)
MistOutMP4: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutMP4: override CPPFLAGS += "-DOUTPUTTYPE=\"output_progressive_mp4.h\""
MistOutMP4: src/output/mist_out.cpp src/output/output.cpp src/output/output_http.cpp src/output/output_progressive_mp4.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutMP3
MistOutMP3: override LDLIBS += $(THREADLIB)
MistOutMP3: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutMP3: override CPPFLAGS += "-DOUTPUTTYPE=\"output_progressive_mp3.h\""
MistOutMP3: src/output/mist_out.cpp src/output/output.cpp src/output/output_http.cpp src/output/output_progressive_mp3.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutRTMP
MistOutRTMP: override LDLIBS += $(THREADLIB)
MistOutRTMP: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutRTMP: override CPPFLAGS += "-DOUTPUTTYPE=\"output_rtmp.h\""
MistOutRTMP: src/output/mist_out.cpp src/output/output.cpp src/output/output_rtmp.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutRaw
MistOutRaw: override LDLIBS += $(THREADLIB)
MistOutRaw: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutRaw: override CPPFLAGS += "-DOUTPUTTYPE=\"output_raw.h\""
MistOutRaw: src/output/mist_out.cpp src/output/output.cpp src/output/output_raw.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutHTTPTS
MistOutHTTPTS: override LDLIBS += $(THREADLIB)
MistOutHTTPTS: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutHTTPTS: override CPPFLAGS += "-DOUTPUTTYPE=\"output_httpts.h\""
MistOutHTTPTS: src/output/mist_out.cpp src/output/output.cpp src/output/output_http.cpp src/output/output_httpts.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutTS
MistOutTS: override LDLIBS += $(THREADLIB)
MistOutTS: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutTS: override CPPFLAGS += "-DOUTPUTTYPE=\"output_ts.h\""
MistOutTS: src/output/mist_out.cpp src/output/output.cpp src/output/output_ts.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutTSPush
MistOutTSPush: override LDLIBS += $(THREADLIB)
MistOutTSPush: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutTSPush: override CPPFLAGS += "-DOUTPUTTYPE=\"output_ts_push.h\""
MistOutTSPush: src/output/mist_out.cpp src/output/output.cpp src/output/output_ts_push.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutHTTP
MistOutHTTP: override LDLIBS += $(THREADLIB)
MistOutHTTP: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutHTTP: override CPPFLAGS += "-DOUTPUTTYPE=\"output_http_internal.h\""
MistOutHTTP: src/output/mist_out.cpp src/output/output.cpp src/output/output_http.cpp src/output/output_http_internal.cpp src/embed.js.h
	$(CXX) $(LDFLAGS) $(CPPFLAGS) src/output/mist_out.cpp src/output/output.cpp src/output/output_http.cpp src/output/output_http_internal.cpp $(LDLIBS) -o $@

outputs: MistOutHSS
MistOutHSS: override LDLIBS += $(THREADLIB)
MistOutHSS: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutHSS: override CPPFLAGS += "-DOUTPUTTYPE=\"output_hss.h\""
MistOutHSS: src/output/mist_out.cpp src/output/output.cpp src/output/output_http.cpp src/output/output_hss.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@
	
outputs: MistOutHLS
MistOutHLS: override LDLIBS += $(THREADLIB)
MistOutHLS: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutHLS: override CPPFLAGS += "-DOUTPUTTYPE=\"output_hls.h\""
MistOutHLS: src/output/mist_out.cpp src/output/output.cpp src/output/output_http.cpp src/output/output_hls.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@
	
outputs: MistOutHDS
MistOutHDS: override LDLIBS += $(THREADLIB)
MistOutHDS: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutHDS: override CPPFLAGS += "-DOUTPUTTYPE=\"output_hds.h\""
MistOutHDS: src/output/mist_out.cpp src/output/output.cpp src/output/output_http.cpp src/output/output_hds.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@
	
outputs: MistOutDASHMP4
MistOutDASHMP4: override LDLIBS += $(THREADLIB)
MistOutDASHMP4: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutDASHMP4: override CPPFLAGS += "-DOUTPUTTYPE=\"output_dash_mp4.h\""
MistOutDASHMP4: src/output/mist_out.cpp src/output/output.cpp src/output/output_http.cpp src/output/output_dash_mp4.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

outputs: MistOutSRT
MistOutSRT: override LDLIBS += $(THREADLIB)
MistOutSRT: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutSRT: override CPPFLAGS += "-DOUTPUTTYPE=\"output_srt.h\""
MistOutSRT: src/output/mist_out.cpp src/output/output_http.cpp src/output/output.cpp src/output/output_srt.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@
	
outputs: MistOutJSON
MistOutJSON: override LDLIBS += $(THREADLIB)
MistOutJSON: override LDLIBS += $(GEOIP) # /*LTS*/
MistOutJSON: override CPPFLAGS += "-DOUTPUTTYPE=\"output_json.h\""
MistOutJSON: src/output/mist_out.cpp src/output/output.cpp src/output/output_http.cpp src/output/output_json.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $^ $(LDLIBS) -o $@

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

src/embed.js.h: src/embed.js sourcery
	$(CLOSURE) src/embed.js > embed.min.js
	./sourcery embed.min.js embed_js > src/embed.js.h
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

