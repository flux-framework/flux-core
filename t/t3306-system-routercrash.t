#!/bin/sh
#

test_description='Hard fail a router node and verify child restarts

When a router node crashes without a chance to notify its children
and then starts back up, the children will reconnect and try to resume
communications.  From the router perspective, the child uuid is
unknown, so it forces a subtree panic in the child.  The maintains the
invariant that when a router restarts, its children always restart too.
This test verifies that the invariant holds when ther router crashes.
'

. `dirname $0`/sharness.sh

export TEST_UNDER_FLUX_FANOUT=1

test_under_flux 3 system

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"

test_expect_success 'tell each broker to log to stderr' '
	flux exec flux setattr log-stderr-mode local
'

test_expect_success 'construct FLUX_URI for rank 2 (child of 1)' '
	echo "local://$(flux getattr rundir)/local-2" >uri2 &&
	test $(FLUX_URI=$(cat uri2) flux getattr rank) -eq 2
'

test_expect_success 'kill -9 broker 1' '
	$startctl kill 1 9 &&
	test_expect_code 137 $startctl wait 1
'

test_expect_success 'restart broker 1' '
	$startctl run 1
'

test_expect_success 'ping broker 1 via broker 2' '
	(FLUX_URI=$(cat uri2) test_must_fail flux ping --count=1 1)
'

test_expect_success 'broker 2 was disconnected' '
	test_might_fail $startctl wait 2
'

test_expect_success 'restart broker 2' '
	$startctl run 2
'

test_expect_success 'wait for rank 0 to report subtree status of full' '
	run_timeout 10 flux overlay status -vvv --pretty --color --wait full
'

test_expect_success 'ping broker 2 via broker 0' '
	flux ping --count=1 2
'

# Side effect: let rc1 on rank 2 finish loading resource module
# before shutdown begins, or it will complain
test_expect_success 'run a 3 node job' '
        flux mini run -n3 -N3 /bin/true
'


test_done
