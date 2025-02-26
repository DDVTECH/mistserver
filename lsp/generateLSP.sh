#!/bin/bash

echo "Minimizing LSP.."

terser -mo minified.js -- plugins/md5.js plugins/cattablesort.js mist.js

if [ $? -eq 0 ]; then
  echo "Done."
else
  exit 1
fi


git add minified.js
echo "Staged."
CONFLICTS=`git diff --name-only --diff-filter=U`
if [ -z "$CONFLICTS" ] ; then
  git status | grep "rebase in progress" > /dev/null && echo "No more conflicts; continuing rebase!" && git rebase --continue
fi

exit 0
