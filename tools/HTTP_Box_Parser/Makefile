SRC = main.cpp ../../util/flv_tag.cpp ../../util/http_parser.cpp ../../util/socket.cpp
OBJ = $(SRC:.cpp=.o)
OUT = Box_Parser
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
	$(CC) $(INCLUDES) $(CCFLAGS) -c $< -o $@
$(OUT): $(OBJ)
	$(CC) $(LIBS) -o $(OUT) $(OBJ)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~

