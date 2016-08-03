#!/bin/sh
#

test_description='Test job log

Test job log.'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh


test_expect_success 'created persistdir' '
	PERSISTDIR=`mktemp -d`
'

test_expect_success 'joblog exists, non-empty if job(s) run' '
	rm -rf $PERSISTDIR/* &&
	flux start -o,--setattr=persist-directory=$PERSISTDIR \
	    flux wreckrun /bin/true &&
	test -f $PERSISTDIR/joblog &&
	test -s $PERSISTDIR/joblog
'

test_expect_success 'joblog exists, empty if no jobs run' '
	rm -rf $PERSISTDIR/* &&
	flux start -o,--setattr=persist-directory=$PERSISTDIR \
	    /bin/true &&
	test -f $PERSISTDIR/joblog &&
	! test -s $PERSISTDIR/joblog
'

test_expect_success 'removed persistdir' '
	rm -rf $PERSISTDIR
'

test_done
