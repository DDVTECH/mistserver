#!/bin/bash
java -jar closure-compiler.jar --warning_level QUIET  plugins/md5.js plugins/cattablesort.js mist.js > minified.js
