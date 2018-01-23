#!/bin/bash
echo "Minimizing LSP..";

if [ "minified.js" -ot "plugins/md5.js" ] || [ "minified.js" -ot "plugins/plugins/cattablesort.js" ] || [ "minified.js" -ot "mist.js" ]; then
  echo "  Generating minified.js.."
  java -jar closure-compiler.jar --warning_level QUIET  plugins/md5.js plugins/cattablesort.js mist.js > minified.js
fi

echo "Done.";
