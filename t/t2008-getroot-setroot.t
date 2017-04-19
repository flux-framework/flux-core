#!/bin/sh
#

test_description='Test kvs-getroot/kvs-setroot

Test recovery from persistdir using flux kvs-setroot.'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh


test_expect_success 'created persistdir' '
	PERSISTDIR=$(mktemp -d --tmpdir=$(pwd))
'
test_expect_success 'kvsroot exists, non-empty' '
	rm -rf $PERSISTDIR/* &&
	flux start -o,--setattr=persist-directory=$PERSISTDIR \
	    flux wreckrun /bin/true &&
	test -f $PERSISTDIR/kvsroot &&
    grep rootdir $PERSISTDIR/kvsroot
'

test_expect_success 'recover kvs in new session from persistdir and kvsroot' '
	flux start -o,--setattr=persist-directory=$PERSISTDIR \
	    "flux kvs-setroot $PERSISTDIR/kvsroot ; flux wreck ls >output" &&
    test_debug "cat output" &&
    test -f output &&
    test -s output
'

test_done
