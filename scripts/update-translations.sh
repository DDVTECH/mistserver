#!/bin/bash
# Refresh both translation templates and sync each into its languages' PO
# files. Run this after adding/changing tr()/trn() calls in lsp/mist.js, or
# tr() calls on backend strings (capa["friendly"|"desc"|"help"|"name"|"hrn"|
# "source_help"] and friends) in src/input|output|process or
# src/controller/controller_capabilities.cpp.
#
# Two independent components, each real xgettext -- no custom string-literal
# parser for either surface:
#   1. mistserver-lsp:     lsp/mist.js, extracted in JavaScript mode
#   2. mistserver-backend: src/input|output|process + controller_capabilities.cpp,
#                          extracted in C++ mode
#
# Usage: scripts/update-translations.sh
set -e

cd "$(dirname "$0")/.."
REPO_ROOT="$(pwd)"

update_component() {
  local name="$1" lang_dir="$2" pot="$3"; shift 3
  mkdir -p "$lang_dir"
  echo "Extracting $name ($*)..."
  xgettext "$@" \
    --from-code=UTF-8 \
    --package-name="$name" \
    --copyright-holder="DDVTech" \
    -o "$pot"
  sed -i 's/charset=CHARSET/charset=UTF-8/' "$pot"

  echo "Syncing existing translations for $name..."
  shopt -s nullglob
  for po in "$lang_dir"/*.po; do
    echo "  msgmerge --update $po"
    msgmerge --quiet --update --backup=off "$po" "$pot"
  done
  shopt -u nullglob
}

update_component mistserver-lsp "$REPO_ROOT/lsp/lang" "$REPO_ROOT/lsp/lang/mistserver-lsp.pot" \
  --language=JavaScript --keyword=tr --keyword=trn:1,2 lsp/mist.js

update_component mistserver-backend "$REPO_ROOT/src/lang" "$REPO_ROOT/src/lang/mistserver-backend.pot" \
  --language=C++ --keyword=tr \
  src/input/*.cpp src/output/*.cpp src/process/*.cpp src/controller/controller_capabilities.cpp

echo "Done."
