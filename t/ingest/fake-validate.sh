#!/bin/bash

while read line
do
    echo '{"errnum": 0}'
done <&0

exit 0
