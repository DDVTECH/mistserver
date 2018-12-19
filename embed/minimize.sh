#!/bin/bash

echo "Minimizing player code..";

echo "  Minimizing JS..";

if [ "min/player.js" -ot "util.js" ] || [ "min/player.js" -ot "skins.js" ] || [ "min/player.js" -ot "controls.js" ] || [ "min/player.js" -ot "player.js" ]; then
  echo "    Minimizing 'util.js skins.js controls.js player.js' into 'min/player.js'..";
  terser -mc -o min/player.js -- util.js skins.js controls.js player.js
fi
echo "  Done.";

echo "    Minimizing wrappers.."

if [ "min/wrappers/dashjs.js" -ot "wrappers/dashjs.js" ]; then
  echo "      Minimizing dashjs";
  terser -mn -o min/wrappers/dashjs.js -- wrappers/dashjs.js
fi
if [ "min/wrappers/flash_strobe.js" -ot "wrappers/flash_strobe.js" ]; then
  echo "      Minimizing flash_strobe";
  terser -mn -o min/wrappers/flash_strobe.js -- wrappers/flash_strobe.js
fi
if [ "min/wrappers/html5.js" -ot "wrappers/html5.js" ]; then
  echo "      Minimizing html5";
  terser -mn -o min/wrappers/html5.js -- wrappers/html5.js
fi
if [ "min/wrappers/videojs.js" -ot "wrappers/videojs.js" ]; then
  echo "      Minimizing videojs";
  terser -mn -o min/wrappers/videojs.js -- wrappers/videojs.js
fi
if [ "min/wrappers/webrtc.js" -ot "wrappers/webrtc.js" ]; then
  echo "      Minimizing webrtc";
  terser -mn -o min/wrappers/webrtc.js -- wrappers/webrtc.js
fi
echo "    Done.";

echo "  Minimizing CSS..";

if [ "min/skins/default.css" -ot "skins/default.css" ] || [ "min/skins/default.css" -ot "skins/general.css" ]; then
  echo "    Minimizing default";
  cleancss --format keep-breaks -o min/skins/default.css skins/general.css skins/default.css
fi
if [ "min/skins/dev.css" -ot "skins/default.css" ] || [ "min/skins/dev.css" -ot "skins/general.css" ] || [ "min/skins/dev.css" -ot "skins/dev.css" ]; then
  echo "    Minimizing dev";
  cleancss --format keep-breaks -o min/skins/dev.css skins/general.css skins/default.css skins/dev.css
fi
echo "  Done.";
echo "Done.";
