#!/bin/sh
#

test_description='Test alternate hash config 

Start a session with a non-default hash type for content/kvs.'

BUG1006="-o,--shutdown-grace=0.1"

nil1="sha1-da39a3ee5e6b4b0d3255bfef95601890afd80709"
nil256="sha256-e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

test_expect_success 'Started instance with content.hash=sha1' '
    OUT=$(flux start ${BUG1006} -o,-Scontent.hash=sha1 \
          flux getattr content.hash) && test "$OUT" = "sha1"
'

test_expect_success 'Started instance with content.hash=sha256' '
    OUT=$(flux start ${BUG1006} -o,-Scontent.hash=sha256 \
          flux getattr content.hash) && test "$OUT" = "sha256"
'

test_expect_success 'Content store nil returns correct hash for sha256' '
    OUT=$(flux start ${BUG1006} -o,-Scontent.hash=sha256 \
          flux content store </dev/null) &&
        test "$OUT" = "$nil256"
'

test_expect_success 'Content store nil returns correct hash for sha1' '
    OUT=$(flux start ${BUG1006} -o,-Scontent.hash=sha1 \
          flux content store </dev/null) &&
        test "$OUT" = "$nil1"
'

test_expect_success 'Attempt to start instance with invalid hash fails hard' '
    test_must_fail flux start -o,-Scontent.hash=wronghash /bin/true
'

test_done
