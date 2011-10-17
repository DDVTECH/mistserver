SRC = main.cpp ../util/json/json_reader.cpp ../util/json/json_value.cpp ../util/json/json_writer.cpp ../util/socket.cpp ../util/http_parser.cpp ../util/md5.cpp ../util/util.cpp
OBJ = $(SRC:.cpp=.o)
OUT = DDV_Controller
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
$(OUT): $(OBJ)
	$(CC) $(LIBS) -o $(OUT) $(OBJ)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~
