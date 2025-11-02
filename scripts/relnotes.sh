#!/bin/bash

LASTTAG=$(git describe --tags --abbrev=0)

# Uncomment URL here if you want Markdown encoded URL to PRs in output:
# URL=https://github.com/chaos/diod/pull

pr() {
    if test -n "$URL"; then
        echo "([#$1]($URL/$1))"
    else
        echo "(#$1)"
    fi
}

echo Changes since $LASTTAG:
git log --pretty='%H' --merges $LASTTAG..HEAD | while read commit; do
    pr=$(git log --pretty=%s -n1 $commit | \
        sed -e 's/Merge pull request #//' -e 's/ from .*//')
    body=$(git log --pretty=%b -n1 $commit)

    echo "${body} $(pr $pr)" | fmt -s \
         | sed -e '1s/^/ * /'    `: # Add bullet for first line`        \
               -e '2,$s/^/   /'  `: # Indent all other lines 3 spaces`  \
               -e '/^\s*$/d'     `: # Remove empty lines`
done

# vi:tabstop=4 shiftwidth=4 expandtab
