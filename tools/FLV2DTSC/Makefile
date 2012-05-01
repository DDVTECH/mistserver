SRC = main.cpp ../../util/flv_tag.cpp ../../util/dtsc.cpp ../../util/amf.cpp ../../util/socket.cpp
OBJ = $(SRC:.cpp=.o)
OUT = MistFLV2DTSC
INCLUDES = 
DEBUG = 4
OPTIMIZE = -g
CCFLAGS = -Wall -Wextra -funsigned-char $(OPTIMIZE) -DDEBUG=$(DEBUG)
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
	$(CC) $(LIBS) -o ../../bin/$(OUT) $(OBJ)
clean:
	rm -rf $(OBJ) ../../bin/$(OUT) Makefile.bak *~
