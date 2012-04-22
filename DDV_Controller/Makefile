SRC = main.cpp ../util/json.cpp ../util/socket.cpp ../util/http_parser.cpp ../util/md5.cpp ../util/config.cpp ../util/procs.cpp ../util/base64.cpp ../util/auth.cpp
OBJ = $(SRC:.cpp=.o)
OUT = DDV_Controller
INCLUDES = 
DEBUG = 4
OPTIMIZE = -g
VERSION = `git describe --tags`
COMPILED_USERNAME = testuser
COMPILED_PASSWORD = 179ad45c6ce2cb97cf1029e212046e81
#COMPILED_PASSWORD = testpass
CCFLAGS = -Wall -Wextra -funsigned-char $(OPTIMIZE) -DDEBUG=$(DEBUG) -DCOMPILED_USERNAME=$(COMPILED_USERNAME) -DCOMPILED_PASSWORD=$(COMPILED_PASSWORD) -DVERSION=$(VERSION)
INSTALL = install
CC = $(CROSS)g++
LD = $(CROSS)ld
AR = $(CROSS)ar
LIBS = -lssl -lcrypto
.SUFFIXES: .cpp 
.PHONY: clean default
default: cversion $(OUT)
.cpp.o:
	$(CC) $(INCLUDES) $(CCFLAGS) -c $< -o $@
$(OUT): $(OBJ)
	$(CC) -o $(OUT) $(OBJ) $(LIBS)
clean:
	rm -rf $(OBJ) $(OUT) Makefile.bak *~
cversion:
	rm -rf ../util/config.o
install: $(OUT)
	$(INSTALL) -D ./$(OUT) $(DESTDIR)/usr/bin/$(OUT)

