#!/bin/sh
#

test_description='Test basic request/response handling


Verify basic request/response/rpc handling.
'

. `dirname $0`/sharness.sh
test_under_flux 2 minimal

test_expect_success 'flux_rpc(3) example runs' '
	${FLUX_BUILD_DIR}/doc/man3/trpc
'

test_expect_success 'flux_rpc_then(3) example runs' '
	${FLUX_BUILD_DIR}/doc/man3/trpc_then
'

test_expect_success 'flux_mrpc(3) example runs' '
	${FLUX_BUILD_DIR}/doc/man3/tmrpc_then
'

test_expect_success 'request: load req module on rank 0' '
	flux module load --rank=0 \
		${FLUX_BUILD_DIR}/t/request/.libs/req.so
'

# FIXME: uses rank-addressed requests which we test below
test_expect_success 'request: load req module on rank 1' '
	flux module load --rank=1 \
		${FLUX_BUILD_DIR}/t/request/.libs/req.so
'

test_expect_success 'request: simple rpc with no payload' '
	${FLUX_BUILD_DIR}/t/request/treq null
'

test_expect_success 'request: simple rpc to rank 0' '
	${FLUX_BUILD_DIR}/t/request/treq --rank 0 null
'

test_expect_success 'request: simple rpc to rank 1' '
	${FLUX_BUILD_DIR}/t/request/treq --rank 1 null
'

test_expect_success 'request: rpc that echos back json payload' '
	${FLUX_BUILD_DIR}/t/request/treq echo
'

test_expect_success 'request: rpc returns expected error' '
	${FLUX_BUILD_DIR}/t/request/treq err
'

test_expect_success 'request: rpc returns expected json' '
	${FLUX_BUILD_DIR}/t/request/treq src 
'

test_expect_success 'request: rpc accepts expected json' '
	${FLUX_BUILD_DIR}/t/request/treq sink
'

test_expect_success 'request: 10K responses received in order' '
	${FLUX_BUILD_DIR}/t/request/treq nsrc
'

test_expect_success 'request: 10K responses received in order, with deferrals' '
	${FLUX_BUILD_DIR}/t/request/treq putmsg 
'

test_expect_success 'request: proxy ping 0 from 1 is 4 hops' '
	${FLUX_BUILD_DIR}/t/request/treq --rank 1 pingzero | grep hops=4
'

test_expect_success 'request: proxy ping 0 from 0 is 3 hops' '
	${FLUX_BUILD_DIR}/t/request/treq --rank 0 pingzero | grep hops=3
'

test_expect_success 'request: proxy ping 1 from 1 is 3 hops' '
	${FLUX_BUILD_DIR}/t/request/treq --rank 1 pingself | grep hops=3
'

test_expect_success 'request: proxy ping upstream from 1 is 4 hop' '
	${FLUX_BUILD_DIR}/t/request/treq --rank 1 pingupstream | grep hops=4
'

# Coproc test

test_expect_success 'request: load coproc module on rank 0' '
	flux module load --rank=0 \
		${FLUX_BUILD_DIR}/t/request/.libs/coproc.so
'

test_expect_success 'request: FLUX_O_COPROC works' '
	${FLUX_BUILD_DIR}/t/request/treq --rank 0 coproc
'

test_expect_success 'request: unloaded coproc module on rank 0' '
	flux module remove --rank=0 coproc
'

# FIXME: test doesn't handle this and leaves RPC unanswered
#test_expect_success 'request: proxy ping any from 0 is ENOSYS' '
#	${FLUX_BUILD_DIR}/src/test/request/treq --rank 0 pingany
#'

test_expect_success 'request: unloaded req module on rank 1' '
	flux module remove --rank=1 req
'

test_expect_success 'request: unloaded req module on rank 0' '
	flux module remove --rank=0 req
'

test_done
