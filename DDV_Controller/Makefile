SRC = main.cpp ../util/json/json_reader.cpp ../util/json/json_value.cpp ../util/json/json_writer.cpp ../util/socket.cpp ../util/http_parser.cpp ../util/md5.cpp ../util/util.cpp
OBJ = $(SRC:.cpp=.o)
OUT = DDV_Controller
INCLUDES = 
DEBUG = 4
OPTIMIZE = -g
COMPILED_USERNAME = testuser
COMPILED_PASSWORD = 179ad45c6ce2cb97cf1029e212046e81
#COMPILED_PASSWORD = testpass
CCFLAGS = -Wall -Wextra -funsigned-char $(OPTIMIZE) -DDEBUG=$(DEBUG) -DCOMPILED_USERNAME=$(COMPILED_USERNAME) -DCOMPILED_PASSWORD=$(COMPILED_PASSWORD)
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
