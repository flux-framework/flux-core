#!/bin/sh

test_description='Test flux overlay trace'

. $(dirname $0)/sharness.sh

test_under_flux 4

waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

test_expect_success 'flux overlay trace fails with extra positional argument' '
	test_must_fail flux overlay trace x y
'
test_expect_success 'flux overlay trace fails with unknown argument' '
	test_must_fail flux overlay trace --not-an-arg
'
test_expect_success 'flux overlay trace fails with wrong message type' '
	test_must_fail flux overlay trace -t foo,bar
'
test_expect_success 'reload heartbeat with increased heart rate' '
	flux module reload heartbeat period=0.2s
'
test_expect_success NO_CHAIN_LINT 'start background trace' '
	flux overlay trace >trace.out &
	echo $! >trace.pid
'
test_expect_success NO_CHAIN_LINT 'start second background trace with --full' '
	flux overlay trace --full >trace2.out &
	echo $! >trace2.pid
'
test_expect_success NO_CHAIN_LINT 'heartbeat.pulse event was captured' '
	$waitfile -t 60 -p heartbeat.pulse trace.out
'
test_expect_success NO_CHAIN_LINT 'heartbeat.pulse event was captured with --full' '
	$waitfile -t 60 -p heartbeat.pulse trace2.out
'
test_expect_success NO_CHAIN_LINT 'send one kvs.ping to rank 1' '
	flux ping -r 1 -c 1 kvs
'
test_expect_success NO_CHAIN_LINT 'kvs.ping request/response was captured' '
	$waitfile -t 60 -c 2 -p kvs.ping trace.out
'
test_expect_success NO_CHAIN_LINT 'kvs.ping request/response was captured with --full' '
	$waitfile -t 60 -c 2 -p kvs.ping trace2.out
'
# This RPC happens to return a human readable error on failure
test_expect_success NO_CHAIN_LINT 'send one job-manager.kill (failing)' '
	test_must_fail flux exec -r 1 flux job kill fuzzybunny
'
test_expect_success NO_CHAIN_LINT 'job-manager.kill request/response was captured' '
	$waitfile -t 60 -c 2 -p "job%-manager.kill" trace.out
'
test_expect_success NO_CHAIN_LINT 'job-manager.kill request/response was captured with --full' '
	$waitfile -t 60 -c 2 -p "job%-manager.kill" trace2.out
'
test_expect_success NO_CHAIN_LINT 'stop background trace' '
	pid=$(cat trace.pid) &&
	kill -15 $pid &&
	wait $pid || true
'
test_expect_success NO_CHAIN_LINT 'stop second background trace' '
	pid=$(cat trace2.pid) &&
	kill -15 $pid &&
	wait $pid || true
'
test_done
