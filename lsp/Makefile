default: server.html
uncompressed:
	cat jquery.js placeholder.js md5.js main.js functions.js > uncompressed.js
compressed: uncompressed
	echo "<script>" > compressed.js
	./closure < uncompressed.js >> compressed.js
	echo "</script>" >> compressed.js
server.html: compressed
	cat header.html compressed.js > ../server.html
	echo "<style>" >> ../server.html
	cat style.css >> ../server.html
	echo "</style>" >> ../server.html
	cat footer.html >> ../server.html
