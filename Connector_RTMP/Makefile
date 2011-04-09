SRC = main.cpp ../util/ddv_socket.cpp ../util/flv_tag.cpp
OBJ = $(SRC:.cpp=.o)
OUT = DDV_Conn_RTMP
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
$(OUT): $(OBJ) chunkstream.cpp parsechunks.cpp handshake.cpp crypto.cpp amf.cpp
	$(CC) -o $(OUT) $(OBJ) $(STATIC) $(LIBS)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~
install: $(OUT)
	-service RTMP_Conn stop
	cp -f ./$(OUT) /usr/bin/
	cp -f ./RTMP_Conn /etc/init.d/
	service RTMP_Conn start

