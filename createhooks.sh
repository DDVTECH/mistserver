#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
echo -e "#!/bin/bash\n[ -f configure ] && touch configure\n[ -f configure.ac ] && touch configure.ac" > $DIR/.git/hooks/post-commit
echo -e "#!/bin/bash\n[ -f configure ] && touch configure\n[ -f configure.ac ] && touch configure.ac" > $DIR/.git/hooks/post-checkout
echo -e "#!/bin/bash\n[ -f configure ] && touch configure\n[ -f configure.ac ] && touch configure.ac" > $DIR/.git/hooks/post-receive
chmod +x $DIR/.git/hooks/post-commit
chmod +x $DIR/.git/hooks/post-checkout
chmod +x $DIR/.git/hooks/post-receive
echo "Done! The version number should now auto-update whenever you commit or checkout."

