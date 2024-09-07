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
test_expect_success NO_CHAIN_LINT 'heartbeat.pulse event was captured' '
	$waitfile -t 60 -p heartbeat.pulse trace.out
'
test_expect_success NO_CHAIN_LINT 'send one kvs.ping to rank 1' '
	flux ping -r 1 -c 1 kvs
'
test_expect_success NO_CHAIN_LINT 'kvs.ping request/response was captured' '
	$waitfile -t 60 -c 2 -p kvs.ping trace.out
'
test_expect_success NO_CHAIN_LINT 'stop background trace' '
	kill -15 $(cat trace.pid); wait || true
'
test_done