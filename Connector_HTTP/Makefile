SRC = main.cpp ../util/socket.cpp ../util/http_parser.cpp ../util/flv_tag.cpp ../util/amf.cpp ../util/dtsc.cpp ../util/config.cpp ../util/base64.cpp
OBJ = $(SRC:.cpp=.o)
OUT = MistConnHTTP
INCLUDES = 
DEBUG = 4
OPTIMIZE = -g
CCFLAGS = -Wall -Wextra -funsigned-char $(OPTIMIZE) -DDEBUG=$(DEBUG) -DVERSION=$(VERSION)
VERSION = `git describe --tags`
CC = $(CROSS)g++
LD = $(CROSS)ld
AR = $(CROSS)ar
LIBS=
.SUFFIXES: .cpp 
.PHONY: clean default
default: cversion $(OUT)
.cpp.o:
	$(CC) $(INCLUDES) $(CCFLAGS) $(LIBS) -c $< -o $@
$(OUT): $(OBJ)
	$(CC) $(LIBS) -o ../bin/$(OUT) $(OBJ)
clean:
	rm -rf $(OBJ) ../bin/$(OUT) Makefile.bak *~
install: $(OUT)
	cp -f ../bin/$(OUT) /usr/bin/
cversion:
	rm -rf ../util/config.o

