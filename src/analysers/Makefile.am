outdir=../..
out_PROGRAMS=MistAnalyserRTMP MistAnalyserFLV MistAnalyserDTSC MistAnalyserAMF
AM_LDFLAGS=-L../../lib
MistAnalyserRTMP_SOURCES=rtmp_analyser.cpp
MistAnalyserRTMP_LDADD=-lrtmpchunks -lamf -lflv_tag -ldtsc -lsocket -lssl -lcrypto
MistAnalyserFLV_SOURCES=flv_analyser.cpp
MistAnalyserFLV_LDADD=-lflv_tag -lamf -ldtsc -lsocket
MistAnalyserDTSC_SOURCES=dtsc_analyser.cpp
MistAnalyserDTSC_LDADD=-ldtsc
MistAnalyserAMF_SOURCES=amf_analyser.cpp
MistAnalyserAMF_LDADD=-lamf

