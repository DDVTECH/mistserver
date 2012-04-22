SRC = main.cpp ../util/socket.cpp ../util/flv_tag.cpp ../util/amf.cpp ../util/rtmpchunks.cpp ../util/crypto.cpp ../util/config.cpp ../util/dtsc.cpp
OBJ = $(SRC:.cpp=.o)
OUT = DDV_Conn_RTMP
INCLUDES =
STATIC =
DEBUG = 4
OPTIMIZE = -g
CCFLAGS = -Wall -Wextra -funsigned-char $(OPTIMIZE) -DDEBUG=$(DEBUG) -DVERSION=$(VERSION)
INSTALL = install
CC = $(CROSS)g++
LD = $(CROSS)ld
AR = $(CROSS)ar
VERSION = `git describe --tags`
LIBS = -lssl -lcrypto
.SUFFIXES: .cpp 
.PHONY: clean default
default: cversion $(OUT)
.cpp.o:
	$(CC) $(INCLUDES) $(CCFLAGS) -c $< -o $@
$(OUT): $(OBJ)
	$(CC) -o $(OUT) $(OBJ) $(STATIC) $(LIBS)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~
install: $(OUT)
	$(INSTALL) -D ./$(OUT) $(DESTDIR)/usr/bin/$(OUT)
cversion:
	rm -rf ../util/config.o

