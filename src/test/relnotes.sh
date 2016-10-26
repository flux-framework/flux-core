#!/bin/bash

LASTTAG=$(git describe --tags --abbrev=0)
URL=https://github.com/flux-framework/flux-core/pull

echo Changes since $LASTTAG:
git log --pretty='format:%s%n%b' --merges $LASTTAG..HEAD | while read line; do
    if echo $line|grep -q "Merge pull"; then
        pr=$(echo $line|sed -e 's/Merge pull request #//' -e 's/ from .*//')
        count=0
    else
        count=$(($count+1))
        if test $count -eq 1; then
            echo "* [#$pr]($URL/$pr) $line"
        else
            echo "$line"
        fi
    fi
done

# vi:tabstop=4 shiftwidth=4 expandtab
