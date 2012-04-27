SRC = main.cpp ../util/json.cpp ../util/socket.cpp ../util/dtsc.cpp ../util/tinythread.cpp user.cpp stats.cpp stream.cpp
OBJ = $(SRC:.cpp=.o)
OUT = MistBuffer
INCLUDES = 
DEBUG = 4
OPTIMIZE = -g
CCFLAGS = -Wall -Wextra -funsigned-char $(OPTIMIZE) -DDEBUG=$(DEBUG)
CC = $(CROSS)g++
LD = $(CROSS)ld
AR = $(CROSS)ar
LIBS = -lpthread
.SUFFIXES: .cpp 
.PHONY: clean default
default: $(OUT)
.cpp.o:
	$(CC) $(INCLUDES) $(CCFLAGS) $(LIBS) -c $< -o $@
$(OUT): $(OBJ)
	$(CC) $(LIBS) -o ../bin/$(OUT) $(OBJ)
clean:
	rm -rf $(OBJ) ../bin/$(OUT) Makefile.bak *~
install: $(OUT)
	cp -f ../bin/$(OUT) /usr/bin/

