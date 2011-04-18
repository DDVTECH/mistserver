SRC = main.cpp ../util/ddv_socket.cpp ../util/flv_tag.cpp ../util/amf.cpp ../util/rtmpchunks.cpp ../util/crypto.cpp
OBJ = $(SRC:.cpp=.o)
OUT = DDV_Conn_RTMP
INCLUDES =
STATIC =
DEBUG = 4
OPTIMIZE = -g
CCFLAGS = -Wall -Wextra -funsigned-char $(OPTIMIZE) -DDEBUG=$(DEBUG)
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
install: $(OUT)
	-service RTMP_Conn stop
	cp -f ./$(OUT) /usr/bin/
	cp -f ./RTMP_Conn /etc/init.d/
	service RTMP_Conn start

