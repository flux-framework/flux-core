#!/bin/sh
#
# FIXME: this test will always succeed as long as the logger
# command thinks everything is OK, a dubious proposition since
# Flux log messages are fire and forget.  Logging needs a makeover,
# and so does this test.

test_description='Test logging service

Verify flux logger in a running flux comms session.
'

. `dirname $0`/sharness.sh
SIZE=4
test_under_flux ${SIZE}

test_expect_success 'logger: log from all ranks works' '
	flux exec flux logger test message
'
test_done
