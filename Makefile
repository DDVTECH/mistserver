prefix = /usr
exec_prefix = $(prefix)
includedir = $(prefix)/include
libdir = $(exec_prefix)/lib

PACKAGE_VERSION := $(shell git describe --tags 2> /dev/null || cat VERSION 2> /dev/null || echo "Unknown")
DEBUG = 4

ifeq ($(PACKAGE_VERSION),Unknown)
  $(warning Version is unknown - consider creating a VERSION file or fixing your git setup.)
endif

CPPFLAGS = -Wall -g -O2 -fPIC
override CPPFLAGS += -funsigned-char -DDEBUG="$(DEBUG)" -DPACKAGE_VERSION="\"$(PACKAGE_VERSION)\""

# /*LTS-START*/
FILLER_DATA =
override CPPFLAGS += -DFILLER_DATA=\"$(FILLER_DATA)\"
ifeq ($(FILLER_DATA),)
  $(warning Filler data is empty and this is an LTS build - did you set FILLER_DATA?)
endif
# /*LTS-END*/

LDLIBS = -lcrypto
THREADLIB = -lpthread -lrt
LDLIBS = -lcrypto $(THREADLIB)


.DEFAULT_GOAL := all

all: libmist.so libmist.a

DOXYGEN := $(shell doxygen -v 2> /dev/null)
ifdef DOXYGEN
all: docs
else
$(warning Doxygen not installed - not building source documentation.)
endif

objects := $(patsubst %.cpp,%.o,$(wildcard lib/*.cpp))


libmist.so: $(objects)
	$(CXX) -shared -o $@ $(LDLIBS) $^

libmist.a: $(objects)
	$(AR) -rcs $@ $^

docs: lib/*.h lib/*.cpp Doxyfile
	doxygen ./Doxyfile > /dev/null

clean:
	rm -f lib/*.o libmist.so libmist.a
	rm -rf ./docs

install: libmist.so libmist.a lib/*.h
	mkdir -p $(DESTDIR)$(includedir)/mist
	install lib/*.h $(DESTDIR)$(includedir)/mist/
	install libmist.so $(DESTDIR)$(libdir)/libmist.so
	install libmist.a $(DESTDIR)$(libdir)/libmist.a
	$(POST_INSTALL)
	ldconfig

uninstall:
	rm -f $(DESTDIR)$(includedir)/mist/*.h
	rmdir $(DESTDIR)$(includedir)/mist
	rm -f $(DESTDIR)$(libdir)/libmist.so
	rm -f $(DESTDIR)$(libdir)/libmist.a

.PHONY: clean uninstall



