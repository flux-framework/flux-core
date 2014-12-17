#!/bin/sh
#

test_description='Test basic request/response handling


Verify basic request/response/rpc handling.
'

. `dirname $0`/sharness.sh
test_under_flux 2

test_expect_success 'request: load req module on rank 0' '
	flux module load ${FLUX_BUILD_DIR}/src/test/request/.libs/req.so
'

# FIXME: uses rank-addressed requests which we test below
test_expect_success 'request: load req module on rank 1' '
	flux module --rank 1 \
		load ${FLUX_BUILD_DIR}/src/test/request/.libs/req.so
'

test_expect_success 'request: simple rpc with no payload' '
	${FLUX_BUILD_DIR}/src/test/request/treq null
'

test_expect_success 'request: simple rpc to rank 0' '
	${FLUX_BUILD_DIR}/src/test/request/treq --rank 0 null
'

test_expect_success 'request: simple rpc to rank 1' '
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

test_expect_success 'request: rpc accepts expected json' '
	${FLUX_BUILD_DIR}/src/test/request/treq sink
'

test_expect_success 'request: 10K responses received in order' '
	${FLUX_BUILD_DIR}/src/test/request/treq nsrc
'

test_expect_success 'request: 10K responses received in order, with deferrals' '
	${FLUX_BUILD_DIR}/src/test/request/treq putmsg 
'

test_expect_success 'request: proxy ping 0 from 1 is one hop' '
	${FLUX_BUILD_DIR}/src/test/request/treq --rank 1 pingzero | grep hops=1
'

test_expect_success 'request: proxy ping 0 from 0 is zero hops' '
	${FLUX_BUILD_DIR}/src/test/request/treq --rank 0 pingzero | grep hops=0
'

test_expect_success 'request: proxy ping 1 from 1 is zero hops' '
	${FLUX_BUILD_DIR}/src/test/request/treq --rank 1 pingself | grep hops=0
'

test_expect_success 'request: proxy ping any from 1 is one hop' '
	${FLUX_BUILD_DIR}/src/test/request/treq --rank 1 pingany | grep hops=1
'

# FIXME: test doesn't handle this and leaves RPC unanswered
#test_expect_success 'request: proxy ping any from 0 is ENOSYS' '
#	${FLUX_BUILD_DIR}/src/test/request/treq --rank 0 pingany
#'

test_expect_success 'request: unloaded req module on rank 1' '
	flux module --rank 1 remove req
'

test_expect_success 'request: unloaded req module on rank 0' '
	flux module remove req
'

test_done
