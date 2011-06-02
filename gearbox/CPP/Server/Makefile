SRC = main.cpp ../../../util/ddv_socket.cpp
OBJ = $(SRC:.cpp=.o)
OUT = Gearbox_Test
INCLUDES = 
DEBUG = 4
OPTIMIZE = -g
CCFLAGS = -Wall -Wextra -funsigned-char $(OPTIMIZE) -DDEBUG=$(DEBUG)
CC = $(CROSS)g++
LD = $(CROSS)ld
AR = $(CROSS)ar
LIBS=
.SUFFIXES: .cpp 
.PHONY: clean default
default: $(OUT)
.cpp.o:
	$(CC) $(INCLUDES) $(CCFLAGS) $(LIBS) -c $< -o $@
$(OUT): $(OBJ)
	$(CC) $(LIBS) -o $(OUT) $(OBJ)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~
install: $(OUT)
	-service HTTP_Conn stop
	cp -f ./$(OUT) /usr/bin/
	cp -f ./HTTP_Conn /etc/init.d/
	service HTTP_Conn start

