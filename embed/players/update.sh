#!/bin/bash

echo "Dashjs"
wget https://cdn.dashjs.org/latest/dash.all.min.js -O dash.all.min.js

echo "Videojs"
echo "You'll want to check for the latest version at https://videojs.com/getting-started/#download-cdn"
wget https://vjs.zencdn.net/8.0.4/video.min.js -O video.min.js

echo "HLSjs"
echo "Releases at https://github.com/video-dev/hls.js/ - download the .zip, extract, and replace hls.js with hls.min.js"
