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

distclean: clean

install: libmist.so libmist.a lib/*.h
	mkdir -p $(DESTDIR)$(includedir)/mist
	install -m 644 lib/*.h $(DESTDIR)$(includedir)/mist/
	mkdir -p $(DESTDIR)$(libdir)
	install -m 644 libmist.a $(DESTDIR)$(libdir)/libmist.a
	install -m 644 libmist.so $(DESTDIR)$(libdir)/libmist.so
	$(POST_INSTALL)
	if [ "$$USER" = "root" ]; then ldconfig; else echo "run: sudo ldconfig"; fi

uninstall:
	rm -f $(DESTDIR)$(includedir)/mist/*.h
	rmdir $(DESTDIR)$(includedir)/mist
	rm -f $(DESTDIR)$(libdir)/libmist.so
	rm -f $(DESTDIR)$(libdir)/libmist.a

.PHONY: clean uninstall



