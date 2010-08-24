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
$(OUT): $(OBJ)
	$(CC) $(LIBS) -o $(OUT) $(OBJ)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~
run-test: $(OUT)
	rm -rf ./meh
	mkfifo ./meh
	cat ./meh &
	nc -l -p 1935 -e './Connector_RTMP 2>./meh'
