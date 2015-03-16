#!/bin/bash

cat header.html > $1
echo "<script>" >> $1
cat plugins/jquery.js plugins/jquery.flot.min.js plugins/jquery.flot.time.min.js plugins/jquery.qrcode.min.js >> $1
cat plugins/md5.js plugins/cattablesort.js mist.js >> $1
echo "</script><style>" >> $1
cat main.css >> $1
echo "</style>" >> $1
cat footer.html >> $1

