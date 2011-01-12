SRC = box.cpp box_ftyp.cpp box_h264.cpp box_stbl.cpp box_stco.cpp box_stsc.cpp box_stsd.cpp box_stts.cpp box_url.cpp box_dref.cpp box_dinf.cpp box_minf.cpp box_vmhd.cpp main.cpp
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
