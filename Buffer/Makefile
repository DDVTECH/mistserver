SRC = main.cpp ../util/json.cpp ../util/socket.cpp ../util/dtsc.cpp ../util/tinythread.cpp
OBJ = $(SRC:.cpp=.o)
OUT = DDV_Buffer
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
	$(CC) $(INCLUDES) $(CCFLAGS) -c $< -o $@ $(LIBS)
$(OUT): $(OBJ)
	$(CC) -o $(OUT) $(OBJ) $(LIBS)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~
install: $(OUT)
	cp -f ./$(OUT) /usr/bin/

