#!/bin/sh -x

# This is a helper for t0004-events.t

base=$1
if test $# -gt 1; then
    rank=$2
    for suffix in foo bar baz eof; do
        flux exec -r $rank flux event pub ${base}.${suffix}
    done
else
    for suffix in foo bar baz eof; do
       echo ${base}.${suffix}
    done
fi
