#!/bin/sh
#
#  Usage: shard-tests.sh I/N
#
#  Read test names on stdin and print those in shard I of N, where
#  1 <= I <= N. Tests are assigned round-robin, so the long running
#  tests at the start of TESTS are spread across shards.
#
#  Example:
#
#    make -C t check \
#        TESTS="$(make -s -C t print-tests | src/test/shard-tests.sh 2/3)"
#
die() { echo "shard-tests.sh: $*" >&2; exit 1; }

case "$1" in
    [0-9]*/[0-9]*) ;;
    *) die "Usage: shard-tests.sh I/N" ;;
esac
i=${1%/*}
n=${1#*/}
case "$i$n" in
    *[!0-9]*) die "invalid shard '$1'" ;;
esac
if test "$i" -lt 1 || test "$i" -gt "$n"; then
    die "invalid shard '$1'"
fi

awk -v i="$i" -v n="$n" 'NR % n == i % n'
