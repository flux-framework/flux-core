#!/bin/sh
#

test_description='Test mecho service

Verify mecho in a running flux comms session.
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE}

test_expect_success 'mecho: mping 0-3 works' '
	flux mping --count 1 \[0-3\]
'
test_expect_success 'mecho: mping 1-2 works' '
	flux mping --count 1 \[1-2\]
'

test_expect_success 'mecho: mping 0 works' '
	flux mping --count 1 0
'

test_expect_success 'mecho: mping bad rank fails' '
	test_must_fail flux mping --count 1 42
'

test_done
