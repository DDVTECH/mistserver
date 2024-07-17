#!/bin/bash

set -euo pipefail

set +e
json="$(./$1 -j)"
status=$?
set -e
echo "$json" > $2
if [[ "$status" == "255" ]]; then
  exit 0
fi
exit $status
