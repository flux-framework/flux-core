#!/bin/bash -e

flux content flush

count=`flux module stats --parse dirty content`
if [ ${count} -ne 0 ]
then
    echo "dirty entries not 0 at start of test"
    return 1
fi

flux module remove content-sqlite

# create a dirty cache entry in the content-cache
flux kvs put issue4379=issue4379bug

count=`flux module stats --parse dirty content`
if [ ${count} -ne 1 ]
then
    echo "dirty entries not 1 after a put"
    return 1
fi

flux module load content-sqlite

count=`flux module stats --parse dirty content`
if [ ${count} -ne 1 ]
then
    echo "dirty entries still 1 after module reload"
    return 1
fi

flux content flush

# Issue 4378 - this dirty count would stay at 1
count=`flux module stats --parse dirty content`
if [ ${count} -ne 0 ]
then
    echo "dirty entries not 0 after final flush"
    return 1
fi
