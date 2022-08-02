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

# Issue 4379 - this dirty count would stay at 1
# Check count in a loop, although highly improbable, technically could
# be racy
count=`flux module stats --parse dirty content`
i=0
while [ ${count} -ne 0 ] && [ $i -lt 50 ]
do
    sleep 0.1
    i=$((i + 1))
done
if [ ${count} -ne 0 ]
then
    echo "dirty entries not 0"
    return 1
fi
