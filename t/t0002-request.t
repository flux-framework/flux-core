#!/bin/sh
#

test_description='Test basic request/response handling

Verify basic request/response/rpc handling.
'

. `dirname $0`/sharness.sh
test_under_flux 1

test_expect_success 'request: load req module' '
	flux module load ${FLUX_BUILD_DIR}/src/test/request/.libs/req.so
'

test_expect_success 'request: null request' '
	${FLUX_BUILD_DIR}/src/test/request/treq
'

test_expect_success 'request: unloaded req module' '
	flux module remove req
'

test_done
