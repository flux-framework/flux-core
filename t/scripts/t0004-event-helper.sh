#!/bin/sh

# This is a helper for t0004-events.t

if test $# -gt 0; then
    rank=$1
    flux exec -r $rank flux event pub testcase.foo
    flux exec -r $rank flux event pub testcase.bar
    flux exec -r $rank flux event pub testcase.baz
    flux exec -r $rank flux event pub testcase.eof
else
    for suffix in foo bar baz; do
       echo testcase.$suffix
    done
    echo testcase.eof
fi
