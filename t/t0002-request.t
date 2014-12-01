#!/bin/sh
#

test_description='Test basic request/response handling

Verify basic request/response/rpc handling.
'

. `dirname $0`/sharness.sh
test_under_flux 2

test_expect_success 'request: load req module' '
	flux module load ${FLUX_BUILD_DIR}/src/test/request/.libs/req.so
'

test_expect_success 'request: simple rpc with no payload' '
	${FLUX_BUILD_DIR}/src/test/request/treq null
'

test_expect_success 'request: simple rpc to rank 0' '
	${FLUX_BUILD_DIR}/src/test/request/treq --rank 0 null
'

# sleep temporarily required - see issue #118
test_expect_success 'request: simple rpc to rank 1' '
	sleep 1 &&
	${FLUX_BUILD_DIR}/src/test/request/treq --rank 1 null
'

test_expect_success 'request: rpc that echos back json payload' '
	${FLUX_BUILD_DIR}/src/test/request/treq echo
'

test_expect_success 'request: rpc returns expected error' '
	${FLUX_BUILD_DIR}/src/test/request/treq err
'

test_expect_success 'request: rpc returns expected json' '
	${FLUX_BUILD_DIR}/src/test/request/treq src 
'

test_expect_success 'request: unloaded req module' '
	flux module remove req
'

test_done
