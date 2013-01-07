CLOSURE=java -jar $(srcdir)/lsp/closure-compiler.jar --warning_level QUIET
EXTRA_DIST=VERSION 
SUBDIRS=src
docs:
	doxygen ./Doxyfile > /dev/null
.PHONY: docs
