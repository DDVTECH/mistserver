#!/bin/bash

CHANGES="$(git diff --name-only --cached | grep embed/)";
readarray -t CHANGES <<<"$CHANGES";

elementIn () {
  local e match="$1";
  shift;
  for e; do [[ "$e" == "$match" ]] && return 0; done;
  return 1;
}

echo "Minimizing player code..";

echo "  Minimizing JS..";

# if elementIn "embed/util.js" "${CHANGES[@]}" || elementIn "embed/skins.js" "${CHANGES[@]}" || elementIn "embed/controls.js" "${CHANGES[@]}" || elementIn "embed/player.js" "${CHANGES[@]}" ; then
  echo "    Minimizing 'util.js skins.js controls.js player.js' into 'min/player.js'..";
  terser -mc -o min/player.js -- util.js skins.js controls.js player.js
#fi
echo "  Done.";

echo "    Minimizing wrappers.."

#if elementIn "embed/wrappers/dashjs.js" "${CHANGES[@]}"; then
  echo "      Minimizing dashjs";
  terser -mn -o min/wrappers/dashjs.js -- wrappers/dashjs.js
#fi
#if elementIn "embed/wrappers/flash_strobe.js" "${CHANGES[@]}"; then
  echo "      Minimizing flash_strobe";
  terser -mn -o min/wrappers/flash_strobe.js -- wrappers/flash_strobe.js
#fi
#if elementIn "embed/wrappers/html5.js" "${CHANGES[@]}"; then
  echo "      Minimizing html5";
  terser -mn -o min/wrappers/html5.js -- wrappers/html5.js
#fi
#if elementIn "embed/wrappers/videojs.js" "${CHANGES[@]}"; then
  echo "      Minimizing videojs";
  terser -mn -o min/wrappers/videojs.js -- wrappers/videojs.js
#fi
#if elementIn "embed/wrappers/webrtc.js" "${CHANGES[@]}"; then
  echo "      Minimizing webrtc";
  terser -mn -o min/wrappers/webrtc.js -- wrappers/webrtc.js
#fi
echo "    Done.";

echo "  Minimizing CSS..";

#if elementIn "embed/skins/default.css" "${CHANGES[@]}" || elementIn "embed/skins/general.css" "${CHANGES[@]}"; #then
  echo "    Minimizing default";
  cleancss --format keep-breaks -o min/skins/default.css skins/general.css skins/default.css
#fi
#if elementIn "embed/skins/default.css" "${CHANGES[@]}" || elementIn "embed/skins/general.css" "${CHANGES[@]}" || elementIn "embed/skins/dev.css" "${CHANGES[@]}"; then
  echo "    Minimizing dev";
  cleancss --format keep-breaks -o min/skins/dev.css skins/general.css skins/default.css skins/dev.css
#fi
echo "  Done.";
echo "Done.";
