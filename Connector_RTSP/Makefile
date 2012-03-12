SRC = main.cpp ../util/socket.cpp ../util/http_parser.cpp ../util/flv_tag.cpp ../util/amf.cpp ../util/util.cpp
OBJ = $(SRC:.cpp=.o)
OUT = Connector_RTSP
INCLUDES = $(shell pkg-config --cflags jrtplib)
DEBUG = 4
OPTIMIZE = -g
CCFLAGS = -Wall -Wextra -funsigned-char $(OPTIMIZE) -DDEBUG=$(DEBUG)
CC = $(CROSS)g++
LD = $(CROSS)ld
AR = $(CROSS)ar
LIBS = $(shell pkg-config --libs jrtplib)
.SUFFIXES: .cpp 
.PHONY: clean default
default: $(OUT)
.cpp.o:
	$(CC) $(INCLUDES) $(CCFLAGS) $(LIBS) -c $< -o $@
$(OUT): $(OBJ)
	$(CC) -o $(OUT) $(OBJ) $(LIBS)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~
run-test: $(OUT)
	rm -rf ./meh
	mkfifo ./meh
	cat ./meh &
	nc -l -p 554 -e './$(OUT) 2>./meh'
