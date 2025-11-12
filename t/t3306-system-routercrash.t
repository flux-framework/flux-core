#!/bin/sh
#

test_description='Hard fail a router node and verify child restarts

When a router node crashes without a chance to notify its children
and then starts back up, the children will reconnect and try to resume
communications.  From the router perspective, the child uuid is
unknown, so it forces a subtree panic in the child.  The maintains the
invariant that when a router restarts, its children always restart too.
This test verifies that the invariant holds when the router crashes.
'

. `dirname $0`/sharness.sh

export TEST_UNDER_FLUX_TOPO=kary:1

# A test below intentionally sends SIGSEGV to a broker process --
# avoid an unnecessary corefile by ensuring the corefile limit is 0
# before calling test_under_flux():
ulimit -c 0

test_under_flux 3 system

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"
groups="flux python ${SHARNESS_TEST_SRCDIR}/scripts/groups.py"

# Usage: waitup N
#   where N is a count of online ranks
waitup () {
        run_timeout 5 flux python -c "import flux; print(flux.Flux().rpc(\"resource.monitor-waitup\",{\"up\":$1}).get())"
}

test_expect_success 'wait for all brokers to be online' '
	${groups} waitfor --count 3 broker.online
'

test_expect_success 'wait for resource module to catch up' '
	waitup 3
'

test_expect_success 'resource status shows no offline nodes' '
        echo 0 >offline0.exp &&
        flux resource status -s offline -no {nnodes} >offline0.out &&
        test_cmp offline0.exp offline0.out
'

test_expect_success 'tell each broker to log to stderr' '
	flux exec flux setattr log-stderr-mode local
'

test_expect_success 'construct FLUX_URI for rank 2 (child of 1)' '
	echo "local://$(flux getattr rundir)/local-2" >uri2 &&
	test $(FLUX_URI=$(cat uri2) flux getattr rank) -eq 2
'

# Choice of signal 11 (SIGSEGV) is deliberate here.
# The broker should not trap this signal - see flux-framework/flux-core#4230.
test_expect_success 'kill -11 broker 1' '
	$startctl kill 1 11 &&
	test_expect_code 139 $startctl wait 1
'

test_expect_success 'wait broker.online to reach count of one' '
	${groups} waitfor --count 1 broker.online
'
test_expect_success 'wait for resource module to catch up' '
	waitup 1
'
test_expect_success 'resource status shows 2 offline nodes' '
        echo 2 >offline2.exp &&
        flux resource status -s offline -no {nnodes} >offline2.out &&
        test_cmp offline2.exp offline2.out
'

test_expect_success 'restart broker 1' '
	$startctl run 1
'

test_expect_success 'wait broker.online to reach count of two' '
	${groups} waitfor --count=2 broker.online
'
test_expect_success 'wait for resource module to catch up' '
	waitup 2
'
test_expect_success 'resource status shows 1 offline nodes' '
        echo 1 >offline1.exp &&
        flux resource status -s offline -no {nnodes} >offline1.out &&
        test_cmp offline1.exp offline1.out
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

test_expect_success 'wait broker.online to reach count of three' '
	${groups} waitfor --count=3 broker.online
'
test_expect_success 'wait for resource module to catch up' '
	waitup 3
'
test_expect_success 'resource status shows 0 offline nodes' '
        flux resource status -s offline -no {nnodes} >offline00.out &&
        test_cmp offline0.exp offline00.out
'

test_expect_success 'wait for rank 0 to report subtree status of full' '
	run_timeout 10 flux overlay status --timeout=0 --wait full
'

test_expect_success 'ping broker 2 via broker 0' '
	flux ping --count=1 2
'

# Side effect: let rc1 on rank 2 finish loading resource module
# before shutdown begins, or it will complain
test_expect_success 'run a 3 node job' '
        flux run -n3 -N3 true
'


test_done
