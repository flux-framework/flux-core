#!/bin/sh

test_description='Test flux startlog command'

. $(dirname $0)/sharness.sh

test_under_flux 2

test_expect_success 'flux-startlog works on rank 0' '
	flux startlog >startlog.out
'
test_expect_success 'there is one run interval' '
	test $(wc -l <startlog.out) -eq 1
'
test_expect_success 'the entry shows the instance is still running' '
	tail -1 startlog.out | grep running
'
test_expect_success 'flux-startlog --check --quiet works' '
	flux startlog --check --quiet
'
test_expect_success 'flux-startlog works on rank 1' '
	flux exec -r 1 flux startlog
'
test_expect_success 'flux-startlog posts start+finish to test eventlog' '
	flux startlog --test-startlog-key=testlog --post-start-event &&
	flux startlog --test-startlog-key=testlog --post-finish-event
'
test_expect_success 'one run interval is listed' '
	flux startlog --test-startlog-key=testlog >testlog.out &&
	test $(wc -l <testlog.out) -eq 1
'
test_expect_success 'it is not shown as running' '
	test_must_fail grep running testlog.out
'
test_expect_success 'flux-startlog --check returns success on test eventlog' '
	flux startlog --test-startlog-key=testlog --check
'
test_expect_success 'flux-startlog posts start to test eventlog' '
	flux startlog --test-startlog-key=testlog --post-start-event
'
test_expect_success 'flux-startlog --check returns success on test eventlog' '
	flux startlog --test-startlog-key=testlog --check
'
test_expect_success 'flux-startlog posts start to test eventlog' '
	flux startlog --test-startlog-key=testlog --post-start-event
'
test_expect_success 'flux-startlog --check returns failure on test eventlog' '
	test_must_fail flux startlog --test-startlog-key=testlog --check
'
test_expect_success 'append unknown version finish event to test eventlog' '
	flux kvs eventlog append testlog finish "{\"version\":99}" &&
	flux kvs eventlog get testlog | tail -1 | grep finish
'
test_expect_success 'flux-startlog --check ignores bogus finish event' '
	test_must_fail flux startlog --test-startlog-key=testlog --check
'
test_expect_success 'flux-startlog --badopt fails' '
	test_must_fail flux startlog --badopt
'
test_expect_success 'flux-startlog cannot post events on rank > 0' '
	test_must_fail flux exec -r 1 flux startlog --post-start-event
'

test_done
