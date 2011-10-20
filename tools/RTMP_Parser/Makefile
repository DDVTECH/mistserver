SRC = main.cpp ../../util/amf.cpp ../../util/rtmpchunks.cpp ../../util/crypto.cpp ../../util/flv_tag.cpp ../../util/socket.cpp
OBJ = $(SRC:.cpp=.o)
OUT = RTMP_Parser
INCLUDES =
STATIC =
CCFLAGS = -Wall -Wextra -funsigned-char -g
CC = $(CROSS)g++
LD = $(CROSS)ld
AR = $(CROSS)ar
LIBS = -lssl -lcrypto
.SUFFIXES: .cpp
.PHONY: clean default
default: $(OUT)
.cpp.o:
	$(CC) $(INCLUDES) $(CCFLAGS) -c $< -o $@
$(OUT): $(OBJ)
	$(CC) -o $(OUT) $(OBJ) $(STATIC) $(LIBS)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~

