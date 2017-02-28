#!/bin/sh

test_description='Test broker security' 

. `dirname $0`/sharness.sh

test_under_flux 4 minimal

test_expect_success 'verify fake munge encoding of messages' '
	${FLUX_BUILD_DIR}/src/test/tmunge --fake
'

test_expect_success 'simulated local connector auth failure returns EPERM' '
	flux comms info &&
	flux module debug --set 1 connector-local &&
	test_must_fail flux comms info 2>authfail.out &&
	grep -q "Operation not permitted" authfail.out
'

test_done
