SRC = main.cpp ../sockets/sw_base.cpp ../sockets/sw_inet.cpp ../sockets/sw_unix.cpp
OBJ = $(SRC:.cpp=.o)
OUT = Connector_RTMP
INCLUDES = 
CCFLAGS = -Wall -Wextra -funsigned-char -g
CC = $(CROSS)g++
LD = $(CROSS)ld
AR = $(CROSS)ar
LIBS = -lssl -lcrypto
.SUFFIXES: .cpp 
.PHONY: clean default
default: $(OUT)
.cpp.o:
	$(CC) $(INCLUDES) $(CCFLAGS) $(LIBS) -c $< -o $@
$(OUT): $(OBJ) chunkstream.cpp parsechunks.cpp handshake.cpp crypto.cpp amf.cpp
	$(CC) $(LIBS) -o $(OUT) $(OBJ)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~
install: $(OUT)
	-service Conn_RTMP stop
	cp -f ./$(OUT) /usr/bin/
	cp -f ./Conn_RTMP /etc/init.d/
	service Conn_RTMP start
