#!/bin/sh -e
# job-shell properly encodes binary data on stdin/out/err

dd if=/dev/urandom bs=1k count=1 > data
cat data | flux run cat >data2
cmp data data2
cat data | flux run sh -c 'cat >&2' 2>data3
# skip the encoding check if spurious warnings appear on stderr
test $(wc -c <data3) -gt 1024 || cmp data data3
exit 0
