SRC = main.cpp ../sockets/sw_base.cpp ../sockets/sw_inet.cpp ../sockets/sw_unix.cpp
OBJ = $(SRC:.cpp=.o)
OUT = Connector_RTSP
INCLUDES =
CCFLAGS = -Wall -Wextra -funsigned-char -g
CC = $(CROSS)g++
LD = $(CROSS)ld
AR = $(CROSS)ar
LIBS = -ljthread -lpthread -ljrtp
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
	nc -l -p 554 -e './$(OUT) 2>./meh'
