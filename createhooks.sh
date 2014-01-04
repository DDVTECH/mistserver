#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
echo -e "#!/bin/bash\nmake clean" > $DIR/.git/hooks/post-commit
echo -e "#!/bin/bash\nmake clean" > $DIR/.git/hooks/post-checkout
echo -e "#!/bin/bash\nmake clean" > $DIR/.git/hooks/post-merge
chmod +x $DIR/.git/hooks/post-commit
chmod +x $DIR/.git/hooks/post-checkout
chmod +x $DIR/.git/hooks/post-merge
echo "Done! The version number should now auto-update whenever you commit or checkout."
