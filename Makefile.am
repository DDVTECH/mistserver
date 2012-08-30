CLOSURE=java -jar $(srcdir)/lsp/closure-compiler.jar --warning_level QUIET
EXTRA_DIST=VERSION lsp/jquery.js lsp/placeholder.js lsp/md5.js lsp/main.js lsp/functions.js
EXTRA_DIST+=lsp/header.html lsp/footer.html lsp/style.css src/embed.js
SUBDIRS=src
docs:
	doxygen ./Doxyfile > /dev/null
.PHONY: docs
