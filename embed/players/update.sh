#!/bin/bash

set -e

cd "$(dirname "$0")"

echo "Dashjs"
wget https://cdn.dashjs.org/latest/dash.all.min.js -O dash.all.min.js

echo "Videojs"
../node_modules/.bin/esbuild videojs.entry.js \
  --bundle \
  --format=esm \
  --legal-comments=inline \
  --minify \
  --banner:js='/*! Video.js 10.0.0-beta.32, hls.js 1.7.1 and dash.js 5.2.1 | Apache-2.0 */' \
  --outfile=video.min.js

echo "HLSjs"
echo "Releases at https://github.com/video-dev/hls.js/releases - download the .zip, extract, and replace hls.js with hls.min.js"
