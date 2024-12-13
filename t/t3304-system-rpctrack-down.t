#!/bin/sh
#

test_description='Test downstream RPC tracking

Test that RPCs from rank 0 broker to other ranks receive
broker-generated EHOSTUNREACH responses when the next hop in
the RPC goes down, either nicely, or a hard crash.
'

. `dirname $0`/sharness.sh

export TEST_UNDER_FLUX_TOPO=kary:1

test_under_flux 3 system

startctl="flux python ${SHARNESS_TEST_SRCDIR}/scripts/startctl.py"

# The health check with wait option is just a convenient RPC that
# can be made to block indefinitely by requesting a state not expected
# to be entered.
test_expect_success NO_CHAIN_LINT 'start background RPC to rank 2' '
	flux overlay status --timeout=0 --wait=lost --rank=2 2>health.err &
	echo $! >health.pid
'

# This ensures the blocking check was received on rank 2 so that
# we don't shut down rank 2 broker before zeromq has routes the request.
test_expect_success 'ensure background request was received on rank 2' '
	flux overlay status --timeout=0 --rank=2
'

test_expect_success NO_CHAIN_LINT 'broker 1 tracked child rpc count is nonzero' '
	count=$(flux exec -r 1 flux module stats --parse=child-rpc overlay) &&
	echo $count &&
	test $count -gt 0
'

test_expect_success 'stop broker 2 nicely and wait for it to exit' '
	$startctl kill 2 15 &&
	$startctl wait 2
'

test_expect_success NO_CHAIN_LINT 'background RPC fails with EHOSTUNREACH (tracker response from rank 1)' '
	pid=$(cat health.pid) &&
	echo waiting for pid $pid &&
	test_expect_code 1 wait $pid &&
	grep "$(strerror_symbol EHOSTUNREACH)" health.err
'

test_expect_success 'new RPC to rank 2 fails with EHOSTUNREACH' '
	test_expect_code 1 flux overlay status \
		--timeout=0 --rank=2 2>health2.err &&
	grep "$(strerror_symbol EHOSTUNREACH)" health2.err
'

test_expect_success 'broker 1 tracked child rpc count is zero' '
	count=$(flux exec -r 1 flux module stats --parse=child-rpc overlay) &&
	echo $count &&
	test $count -eq 0
'

test_expect_success NO_CHAIN_LINT 'start background RPC to rank 1' '
	flux overlay status --timeout=0 --wait=lost --rank=1 2>health3.err &
	echo $! >health3.pid
'

test_expect_success 'ensure background request was received on rank 1' '
	flux overlay status --timeout=0 --rank=1
'

test_expect_success NO_CHAIN_LINT 'broker 0 tracked child rpc count is nonzero' '
	count=$(flux module stats --parse=child-rpc overlay) &&
	echo $count &&
	test $count -gt 0
'

test_expect_success 'stop broker 1 hard and wait for it to exit' '
	$startctl kill 1 9 &&
	($startctl wait 1 || true)
'

# Ensure an EHOSTUNREACH is encountered on the socket to trigger connected
# state change.  Note: existing traffic like heartbeat probably also serve
# this purpose, but do it anyway to ensure repeatability.
test_expect_success 'ping to rank 1 fails with EHOSTUNREACH' '
	echo "flux-ping: 1!broker.ping: $(strerror_symbol EHOSTUNREACH)" >ping.exp &&
	test_must_fail flux ping 1 2>ping.err &&
	test_cmp ping.exp ping.err
'

test_expect_success NO_CHAIN_LINT 'background RPC fails with EHOSTUNREACH (tracker response from rank 0)' '
	pid=$(cat health3.pid) &&
	echo waiting for pid $pid &&
	test_expect_code 1 wait $pid &&
	grep "$(strerror_symbol EHOSTUNREACH)" health3.err
'

test_expect_success 'new RPC to rank 1 fails with EHOSTUNREACH' '
	test_expect_code 1 flux overlay status \
		--timeout=0 --rank=1 2>health4.err &&
	grep "$(strerror_symbol EHOSTUNREACH)" health4.err
'

test_expect_success 'broker 0 tracked child rpc count is zero' '
	count=$(flux module stats --parse=child-rpc overlay) &&
	echo $count &&
	test $count -eq 0
'

test_done
