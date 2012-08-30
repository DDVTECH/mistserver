CLOSURE=java -jar $(srcdir)/lsp/closure-compiler.jar --warning_level QUIET
EXTRA_DIST=server.html server.html.h VERSION lsp/*.js lsp/*.html lsp/*.css src/embed.js*
SUBDIRS=. src
BUILT_SOURCES=lsp/compressed.js server.html.h src/embed.js.h
CLEANFILES=$(BUILT_SOURCES)
docs:
	doxygen ./Doxyfile > /dev/null
.PHONY: docs

SOURCES=server.html.h

lspSOURCES=$(srcdir)/lsp/jquery.js $(srcdir)/lsp/placeholder.js $(srcdir)/lsp/md5.js $(srcdir)/lsp/main.js $(srcdir)/lsp/functions.js
lsp/compressed.js: $(lspSOURCES)
	-mkdir lsp
	$(CLOSURE) --js_output_file $@ $(lspSOURCES)
src/embed.js.h: $(srcdir)/src/embed.js
	$(CLOSURE) --js_output_file embed.js $<
	xxd -i embed.js embed.js.h
	rm embed.js
	mv embed.js.h src/

server.html: lsp/compressed.js
	cat $(srcdir)/lsp/header.html lsp/compressed.js > $@
	echo "<script>" >> $@
	cat lsp/compressed.js >> $@
	echo "</script><style>" >> $@
	cat $(srcdir)/lsp/style.css >> $@
	echo "</style>" >> $@
	cat $(srcdir)/lsp/footer.html >> $@
server.html.h: server.html
	xxd -i server.html server.html.h
