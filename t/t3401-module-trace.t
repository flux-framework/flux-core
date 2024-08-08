#!/bin/sh

test_description='Test flux module trace'

. $(dirname $0)/sharness.sh

test_under_flux 1

waitfile="${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua"

test_expect_success 'flux module trace fails with missing module name' '
	test_must_fail flux module trace
'
test_expect_success 'flux module trace fails with unknown argument' '
	test_must_fail flux module trace --not-an-arg
'
test_expect_success 'flux module trace fails with wrong message type' '
	test_must_fail flux module trace -t foo,bar
'
test_expect_success 'reload heartbeat with increased heart rate' '
	flux module reload heartbeat period=0.2s
'
test_expect_success NO_CHAIN_LINT 'start background trace' '
	flux module trace kvs >trace.out &
	echo $! >trace.pid
'
test_expect_success NO_CHAIN_LINT 'heartbeat.pulse event was captured' '
	$waitfile -t 60 -p heartbeat.pulse trace.out
'
test_expect_success NO_CHAIN_LINT 'send one kvs.ping' '
	flux ping -c 1 kvs
'
test_expect_success NO_CHAIN_LINT 'kvs.ping request/response was captured' '
	$waitfile -t 60 -c 2 -p kvs.ping trace.out
'
test_expect_success NO_CHAIN_LINT 'stop background trace' '
	kill -15 $(cat trace.pid); wait || true
'

test_expect_success NO_CHAIN_LINT 'start background trace on multiple modules' '
	flux module trace kvs barrier >trace2.out &
	echo $! >trace2.pid
'
test_expect_success NO_CHAIN_LINT 'heartbeat.pulse event was captured' '
	$waitfile -t 60 -p heartbeat.pulse trace2.out
'
test_expect_success NO_CHAIN_LINT 'send one barrier.ping' '
	flux ping -c 1 barrier
'
test_expect_success NO_CHAIN_LINT 'barrier.ping request/response was captured' '
	$waitfile -t 60 -c 2 -p barrier.ping trace2.out
'
test_expect_success NO_CHAIN_LINT 'stop background trace' '
	kill -15 $(cat trace2.pid); wait || true
'
test_done
