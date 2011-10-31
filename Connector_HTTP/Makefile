SRC = main.cpp ../util/socket.cpp ../util/http_parser.cpp ../util/flv_tag.cpp ../util/amf.cpp ../util/util.cpp
OBJ = $(SRC:.cpp=.o)
OUT = DDV_Conn_HTTP
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
	$(CC) $(LIBS) -o $(OUT) $(OBJ)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~
install: $(OUT)
	cp -f ./$(OUT) /usr/bin/
cversion:
	rm -rf ../util/util.o

