#!/bin/sh
#

test_description='Test namespace service

Test broker-resident namespace service
'

. `dirname $0`/sharness.sh

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} minimal
echo "# $0: flux session size will be ${SIZE}"

# Test command usage:
#   ${NS} create name flags seq json_str  (flags: SEQUENCED=1, SYNC=2)
#   ${NS} remove name
#   ${NS} lookup name min_seq flags (flags: WAIT=1)
#   ${NS} commit name seq json_str
CREATE_FLAGS=3 # SEQUENCED | SYNC
LOOKUP_NONBLOCK=0
LOOKUP_BLOCK=1
NS=${FLUX_BUILD_DIR}/t/kvs/namespace
USERID_UNKNOWN=0xFFFFFFFF

# next connection via local connector will be as USER not OWNER role
connect_guest_next() {
    flux module debug --set 4 connector-local
}

test_expect_success 'namespace create works' '
	before=$(flux module stats --parse namespaces ns) &&
	${NS} create ns-test ${USERID_UNKNOWN} ${CREATE_FLAGS} &&
	after=$(flux module stats --parse namespaces ns) &&
	test ${after} -eq $((${before}+1))
'

test_expect_success 'namespace create fails if namespace exists' '
	before=$(flux module stats --parse namespaces ns) &&
	! ${NS} create ns-test ${USERID_UNKNOWN} ${CREATE_FLAGS} &&
	after=$(flux module stats --parse namespaces ns) &&
	test ${after} -eq ${before}
'

test_expect_success 'namespace lookup fails before first commit' '
	test_must_fail ${NS} lookup ns-test 0 ${LOOKUP_NONBLOCK}
'

test_expect_success 'namespace commit works (seq=0)' '
	${NS} commit ns-test 0 "{}" &&
	echo "0 {}" >ns-test.0
'

test_expect_success 'namespace lookup works, expected value' '
	${NS} lookup ns-test 0 ${LOOKUP_NONBLOCK} >lookup.0 &&
	test_cmp ns-test.0 lookup.0
'

test_expect_success 'namespace lookup (wait=1) works rank>0, expected values' '
	for rank in $(seq 1 $((${SIZE}-1))); do \
          flux exec -r ${rank} \
	      ${NS} lookup ns-test 0 ${LOOKUP_BLOCK} >lookup.0.r${rank} &&
	          test_cmp ns-test.0 lookup.0.r${rank}
	done
'

test_expect_success 'namespace commit works (seq=1), verified with lookup' '
	${NS} commit ns-test 1 "{}" &&
	echo "1 {}" >ns-test.1 &&
	${NS} lookup ns-test 1 ${LOOKUP_NONBLOCK} >lookup.1 &&
	test_cmp ns-test.1 lookup.1
'

test_expect_success 'namespace lookup (wait=1) works rank>0, expected values' '
	for rank in $(seq 1 $((${SIZE}-1))); do \
          flux exec -r ${rank} \
	      ${NS} lookup ns-test 1 ${LOOKUP_BLOCK} >lookup.1.r${rank} &&
	          test_cmp ns-test.1 lookup.1.r${rank}
	done
'

test_expect_success 'namespace commit (seq=3) fails due to seq skip' '
	test_must_fail ${NS} commit ns-test 3 "{}"
'

test_expect_success 'previous state unchanged on all ranks' '
	for rank in $(seq 0 $((${SIZE}-1))); do \
          flux exec -r ${rank} \
	      ${NS} lookup ns-test 1 ${LOOKUP_BLOCK} >lookup.1.r${rank} &&
	          test_cmp ns-test.1 lookup.1.r${rank}
	done
'

test_expect_success 'namespace commit (seq=2) fails on slave rank' '
	test_must_fail flux exec -r 1 ${NS} commit ns-test 2 "{}"
'

test_expect_success 'namespace event generated on commit' '
        run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace-bypass.lua \
	        ns ns.test.end \
		"flux event pub ns.test.a; \
		${NS} commit ns-test 2 \{\}; \
		flux event pub ns.test.end" >ev.out &&
	grep -q ns.allcommit.ns-test ev.out
'

test_expect_success 'guest cannot see owner namespace event' '
	connect_guest_next &&
        run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace-bypass.lua \
	        ns ns.test.end \
		"flux event pub ns.test.a; \
		${NS} commit ns-test 3 \{\}; \
		flux event pub ns.test.end" >evguest.out &&
	! grep -q ns.allcommit.ns-test evguest.out
'

test_expect_success 'namespace remove works' '
	before=$(flux module stats --parse namespaces ns) &&
	${NS} remove ns-test &&
	after=$(flux module stats --parse namespaces ns) &&
	test ${after} -eq $((${before}-1))
'

test_expect_success 'namespace remove fails if namespace does not exist' '
	test_must_fail ${NS} remove ns-test
'

test_expect_success 'namespace lookup fails if namespace does not exist' '
	test_must_fail ${NS} lookup ns-test 0 ${LOOKUP_NONBLOCK}
'

test_expect_success 'namespace commit fails if namespace does not exist' '
	test_must_fail ${NS} commit ns-test 4 "{}"
'

test_expect_success 'namespace create + first commit works' '
	before=$(flux module stats --parse namespaces ns) &&
	${NS} create ns-test2 ${USERID_UNKNOWN} ${CREATE_FLAGS} &&
	after=$(flux module stats --parse namespaces ns) &&
	test ${after} -eq $((${before}+1)) &&
	${NS} commit ns-test2 0 "{}"
'

test_expect_success 'blocking namespace lookup works' '
	before=$(flux module stats --parse waiters ns) &&
	${NS} lookup ns-test2 1 ${LOOKUP_BLOCK} >lookup2.1 &
	while test $(flux module stats --parse waiters ns) -eq ${before}; do \
	    /bin/true; done &&
	test $(flux module stats --parse waiters ns) -eq $((${before}+1)) &&
	${NS} commit ns-test2 1 "{}" &&
	echo "1 {}" >ns-test2.1 &&
	run_timeout 5 wait &&
	test_cmp ns-test2.1 lookup2.1 &&
	after=$(flux module stats --parse waiters ns) &&
       	test ${after} -eq ${before}
'

test_expect_success 'blocking namespace lookup cleans up when interrupted' '
	before=$(flux module stats --parse waiters ns) &&
	! run_timeout 1 ${NS} lookup ns-test2 2 ${LOOKUP_BLOCK} &&
	after=$(flux module stats --parse waiters ns) &&
	test ${before} -eq ${after}
'

test_expect_success 'guest cannot lookup owner namespace' '
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${FAKEOTHERUSER} \
	    ${NS} lookup ns-test2 0 ${LOOKUP_NONBLOCK}
'

test_expect_success 'guest cannot commit to owner namespace' '
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${FAKEOTHERUSER} \
	    ${NS} commit ns-test2 2 "{}"
'

test_expect_success 'namespace remove works' '
	before=$(flux module stats --parse namespaces ns) &&
	${NS} remove ns-test2 &&
	after=$(flux module stats --parse namespaces ns) &&
	test ${after} -eq $((${before}-1))
'

FAKEUSER=42
FAKEOTHERUSER=43

test_expect_success 'guest cannot create namespaces for themselves' '
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${FAKEUSER} \
	    ${NS} create ns-test3 ${FAKEUSER} ${CREATE_FLAGS}
'
test_expect_success 'instance owner can create namespace for guest' '
	${NS} create ns-test3 ${FAKEUSER} ${CREATE_FLAGS}
'

test_expect_success 'instance owner can commit to guest namespace' '
	${NS} commit ns-test3 0 "{}"
'

test_expect_success 'instance owner can lookup guest namespace' '
	${NS} lookup ns-test3 0 ${LOOKUP_NONBLOCK}
'

test_expect_success 'guest can commit to their namespace' '
	FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${FAKEUSER} \
	    ${NS} commit ns-test3 1 "{}"
'

test_expect_success 'guest can lookup their namespace' '
	FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${FAKEUSER} \
	    ${NS} lookup ns-test3 0 ${LOOKUP_NONBLOCK}
'

test_expect_success 'guest cannot commit to another guests namespace' '
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${FAKEOTHERUSER} \
	    ${NS} commit ns-test3 2 "{}"
'

test_expect_success 'guest cannot lookup another guests namespace' '
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${FAKEOTHERUSER} \
	    ${NS} lookup ns-test3 0 ${LOOKUP_NONBLOCK}
'

# here the "other guest" is FLUX_USERID_UNKNOWN
test_expect_success 'guest cannot see another guests namespace events' '
	connect_guest_next &&
        run_timeout 5 \
	    $SHARNESS_TEST_SRCDIR/scripts/event-trace-bypass.lua \
	        ns ns.test.end \
		"flux event pub ns.test.a; \
		${NS} commit ns-test3 3 \{\}; \
		flux event pub ns.test.end" >evguest2.out &&
	! grep -q ns.allcommit.ns-test3 evguest2.out
'

test_expect_success 'guest cannot remove another guests namespace' '
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${FAKEOTHERUSER} \
	    ${NS} remove ns-test3
'

test_expect_success 'guest cannot remove their namespace' '
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=${FAKEUSER} \
	    ${NS} remove ns-test3
'

test_expect_success 'instance owner can remove guest namespace' '
	${NS} remove ns-test3
'

test_done
