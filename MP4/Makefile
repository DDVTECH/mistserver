SRC = box_avcC.cpp box.cpp box_dinf.cpp box_dref.cpp box_esds.cpp box_ftyp.cpp box_hdlr.cpp box_hmhd.cpp box_mdhd.cpp box_mdia.cpp box_minf.cpp box_moov.cpp box_mvhd.cpp box_nmhd.cpp box_smhd.cpp box_stbl.cpp box_stco.cpp box_stsc.cpp box_stsd.cpp box_stts.cpp box_tkhd.cpp box_trak.cpp box_url.cpp box_vmhd.cpp main.cpp interface.cpp box_mdat.cpp box_rtmp.cpp box_amhp.cpp box_mvex.cpp box_trex.cpp box_afra.cpp box_abst.cpp box_asrt.cpp
OBJ = $(SRC:.cpp=.o)
OUT = Boxtest
INCLUDES = 
CCFLAGS = -Wall -Wextra -funsigned-char -g
CC = $(CROSS)g++
LD = $(CROSS)ld
AR = $(CROSS)ar
LIBS = 
.SUFFIXES: .cpp 
.PHONY: clean default
default: $(OUT)
.cpp.o:
	$(CC) $(INCLUDES) $(CCFLAGS) $(LIBS) -c $< -o $@
$(OUT): $(OBJ)
	$(CC) $(LIBS) -o $(OUT) $(OBJ)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~
