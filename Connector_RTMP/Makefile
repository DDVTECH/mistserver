SRC = main.cpp
OBJ = $(SRC:.cpp=.o)
OUT = Connector_RTMP
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
$(OUT): $(OBJ) handshake.cpp chunkstream.cpp amf.cpp
	$(CC) $(LIBS) -o $(OUT) $(OBJ)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~
run-test: $(OUT)
	rm -rf ./meh
	mkfifo ./meh
	nc -o test -l -p 1935 -e './Connector_RTMP 2>./meh'
run-cat:
	cat < ./meh
