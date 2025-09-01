#!/bin/bash

#CHANGES="$(git diff --name-only --cached | grep embed/)";
#readarray -t CHANGES <<<"$CHANGES";

elementIn () {
  local e match="$1";
  shift;
  for e; do [[ "$e" == "$match" ]] && return 0; done;
  return 1;
}

ERRORS=0

echo "Minimizing player code..";

echo "  Minimizing JS..";

# if elementIn "embed/util.js" "${CHANGES[@]}" || elementIn "embed/skins.js" "${CHANGES[@]}" || elementIn "embed/controls.js" "${CHANGES[@]}" || elementIn "embed/player.js" "${CHANGES[@]}" ; then
  echo "    Minimizing 'util.js skins.js controls.js player.js' into 'min/player.js'..";
  terser -mc -o min/player.js -- util.js skins.js controls.js player.js
  ERRORS=$((ERRORS + $?))
#fi
echo "  Done.";

echo "    Minimizing wrappers.."

#if elementIn "embed/wrappers/dashjs.js" "${CHANGES[@]}"; then
  echo "      Minimizing dashjs";
  terser -mn -o min/wrappers/dashjs.js -- wrappers/dashjs.js
  ERRORS=$((ERRORS + $?))
#fi
#if elementIn "embed/wrappers/flash_strobe.js" "${CHANGES[@]}"; then
  echo "      Minimizing flash_strobe";
  terser -mn -o min/wrappers/flash_strobe.js -- wrappers/flash_strobe.js
  ERRORS=$((ERRORS + $?))
#fi
#if elementIn "embed/wrappers/html5.js" "${CHANGES[@]}"; then
  echo "      Minimizing html5";
  terser -mn -o min/wrappers/html5.js -- wrappers/html5.js
  ERRORS=$((ERRORS + $?))
#fi
#if elementIn "embed/wrappers/videojs.js" "${CHANGES[@]}"; then
  echo "      Minimizing videojs";
  terser -mn -o min/wrappers/videojs.js -- wrappers/videojs.js
  ERRORS=$((ERRORS + $?))
#fi
#if elementIn "embed/wrappers/webrtc.js" "${CHANGES[@]}"; then
  echo "      Minimizing wheprtc";
  terser -mn -o min/wrappers/wheprtc.js -- wrappers/wheprtc.js
  ERRORS=$((ERRORS + $?))
  echo "      Minimizing webrtc";
  terser -mn -o min/wrappers/webrtc.js -- wrappers/webrtc.js
  ERRORS=$((ERRORS + $?))
#fi
  echo "      Minimizing mews";
  terser -mn -o min/wrappers/mews.js -- wrappers/mews.js
  ERRORS=$((ERRORS + $?))
  echo "      Minimizing flv.js";
  terser -mn -o min/wrappers/flv.js -- wrappers/flv.js
  ERRORS=$((ERRORS + $?))
  echo "      Minimizing hls.js";
  terser -mn -o min/wrappers/hlsjs.js -- wrappers/hlsjs.js
  ERRORS=$((ERRORS + $?))
  echo "      Minimizing rawws.js";
  terser -mn -o min/wrappers/rawws.js -- wrappers/rawws.js
  ERRORS=$((ERRORS + $?))
  echo "    Done.";

echo "  Minimizing CSS..";

#if elementIn "embed/skins/default.css" "${CHANGES[@]}" || elementIn "embed/skins/general.css" "${CHANGES[@]}"; #then
  echo "    Minimizing default";
  cleancss --format keep-breaks -o min/skins/default.css skins/general.css skins/default.css
  ERRORS=$((ERRORS + $?))
#fi
#if elementIn "embed/skins/default.css" "${CHANGES[@]}" || elementIn "embed/skins/general.css" "${CHANGES[@]}" || elementIn "embed/skins/dev.css" "${CHANGES[@]}"; then
  echo "    Minimizing dev";
  cleancss --format keep-breaks -o min/skins/dev.css skins/general.css skins/default.css skins/dev.css
  ERRORS=$((ERRORS + $?))
#fi
echo "  Done.";
echo "Done.";

if [ $ERRORS -eq 0 ]; then
  git add min
  echo "Staged."

  CONFLICTS=`git diff --name-only --diff-filter=U`
  if [ -z "$CONFLICTS" ] ; then
    git status | grep "rebase in progress" > /dev/null && echo "No more conflicts; continuing rebase!" && git rebase --continue
  fi
  exit 0
else
  exit $ERRORS
fi

