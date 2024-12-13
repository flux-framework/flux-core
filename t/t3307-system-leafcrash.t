#!/bin/sh
#

test_description='Hard fail a leaf node and verify transition thru lost

When a leaf node crashes without a chance to notify its parent and then
starts back up, it will try to say hello to its parent as though it were
starting anew.  From the parent perspective, the child may be already
online with a different uuid, so upon receipt of the hello request, it
transitions the child subtree status through lost to fail any pending
RPCs, then proceeds to handle the hello.  This test verifies that the
transition through lost occurs.
'

. `dirname $0`/sharness.sh

#
#  With --chain-lint, most tests below are skipped and this can cause
#  the final test to hang. Therefore just skip all tests with chain-lint.
#
if ! test_have_prereq NO_CHAIN_LINT; then
	skip_all='test may hang with --chain-lint, skipping all.'
	test_done
fi

test_under_flux 2 system

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"

test_expect_success 'tell brokers to log to stderr' '
	flux exec flux setattr log-stderr-mode local
'

# Degraded at parent means child was lost
test_expect_success 'start overlay status wait in the background' '
	flux overlay status --timeout=0 --wait degraded &
	echo $! >subtree.pid
'

test_expect_success 'kill -9 broker 1' '
	$startctl kill 1 9 &&
	test_expect_code 137 $startctl wait 1
'

test_expect_success 'restart broker 1' '
	$startctl run 1
'

test_expect_success 'ensure child was lost' '
	wait $(cat subtree.pid)
'

test_expect_success 'and child returned to service' '
	flux overlay status --timeout=0 --wait full
'

# Side effect: let rc1 on rank 1 finish loading resource module
# before shutdown begins, or it will complain
test_expect_success 'run a 2 node job' '
	flux run -n2 -N2 true
'

test_done
