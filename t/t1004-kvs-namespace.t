#!/bin/sh

test_description='Test flux-kvs and kvs in flux session

These are tests for ensuring multiple namespaces work.
'

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} kvs
echo "# $0: flux session size will be ${SIZE}"

DIR=test.a.b
KEY=test.a.b.c

waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

# Just in case its set in the environment
unset FLUX_KVS_NAMESPACE

PRIMARYNAMESPACE=primary
NAMESPACETEST=namespacetest
NAMESPACETMP=namespacetmp
NAMESPACERANK1=namespacerank1
NAMESPACEORDER=namespaceorder

namespace_create_loop() {
        i=0
        while ! flux kvs namespace create $1 && [ $i -lt ${KVS_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

get_kvs_namespace_all_ranks_loop() {
        i=0
        while ! flux exec -n sh -c "flux kvs get --namespace=$1 $2" \
              && [ $i -lt ${KVS_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

get_kvs_namespace_fails_all_ranks_loop() {
        i=0
        while ! flux exec -n sh -c "! flux kvs get --namespace=$1 $2" \
              && [ $i -lt ${KVS_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

wait_fencecount_nonzero() {
        i=0
        while [ "$(flux exec -n -r $1 sh -c "flux module stats --parse namespace.$2.#transactions kvs" 2> /dev/null)" = "0" ] \
              && [ $i -lt ${KVS_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

#
# Basic tests in default primary namespace
#

test_expect_success 'kvs: primary namespace exists/is listed' '
        flux kvs namespace list | grep primary
'

test_expect_success 'kvs: create primary namespace fails' '
	! flux kvs namespace create $PRIMARYNAMESPACE
'

test_expect_success 'kvs: remove primary namespace fails' '
	! flux kvs namespace remove $PRIMARYNAMESPACE
'

test_expect_success 'kvs: get with primary namespace works' '
        flux kvs put --json $DIR.test=1 &&
        test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 1
'

test_expect_success 'kvs: put with primary namespace works' '
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.test=2 &&
        test_kvs_key $DIR.test 2
'

test_expect_success 'kvs: put/get with primary namespace works' '
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.test=3 &&
        test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 3
'

test_expect_success 'kvs: unlink with primary namespace works' '
        flux kvs unlink --namespace=$PRIMARYNAMESPACE $DIR.test &&
        test_must_fail flux kvs get --json $DIR.test
'

test_expect_success 'kvs: dir with primary namespace works' '
        flux kvs put --json $DIR.a=1 &&
        flux kvs put --json $DIR.b=2 &&
        flux kvs put --json $DIR.c=3 &&
        flux kvs dir --namespace=$PRIMARYNAMESPACE $DIR | sort > output &&
        cat >expected <<EOF &&
$DIR.a = 1
$DIR.b = 2
$DIR.c = 3
EOF
        test_cmp expected output
'

test_expect_success 'kvs: unlink dir primary namespace works' '
        flux kvs unlink --namespace=$PRIMARYNAMESPACE -Rf $DIR &&
        ! flux kvs dir --namespace=$PRIMARYNAMESPACE $DIR
'

test_expect_success NO_CHAIN_LINT 'kvs: wait on primary namespace works' '
        VERS=$(flux kvs version)
        VERS=$((VERS + 1))
        flux kvs wait --namespace=$PRIMARYNAMESPACE $VERS &
        kvswaitpid=$!
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.xxx=99
        test_expect_code 0 wait $kvswaitpid
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a key in primary namespace works'  '
        flux kvs unlink --namespace=$PRIMARYNAMESPACE -Rf $DIR &&
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.watch=0 &&
        rm -f watch_out
        flux kvs watch --namespace=$PRIMARYNAMESPACE -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "0" watch_out
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.watch=1 &&
        wait $watchpid
cat >expected <<-EOF &&
0
1
EOF
        test_cmp watch_out expected
'

#
# Basic tests in new namespace
#

test_expect_success 'kvs: namespace create on rank 0 works' '
	flux kvs namespace create $NAMESPACETEST
'

test_expect_success 'kvs: new namespace exists/is listed' '
        flux kvs namespace list | grep $NAMESPACETEST
'

test_expect_success 'kvs: namespace create on rank 1 works' '
	flux exec -n -r 1 sh -c "flux kvs namespace create $NAMESPACERANK1"
'

test_expect_success 'kvs: new namespace exists/is listed' '
        flux kvs namespace list | grep $NAMESPACERANK1
'

test_expect_success 'kvs: put/get value in new namespace works' '
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.test=1 &&
        test_kvs_key_namespace $NAMESPACETEST $DIR.test 1
'

test_expect_success 'kvs: unlink in new namespace works' '
        flux kvs unlink --namespace=$NAMESPACETEST $DIR.test &&
        ! flux kvs get --namespace=$NAMESPACETEST --json $DIR.test
'

test_expect_success 'kvs: dir in new namespace works' '
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.a=4 &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.b=5 &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.c=6 &&
        flux kvs dir --namespace=$NAMESPACETEST $DIR | sort > output &&
        cat >expected <<EOF &&
$DIR.a = 4
$DIR.b = 5
$DIR.c = 6
EOF
        test_cmp expected output
'

test_expect_success 'kvs: unlink dir in new namespace works' '
        flux kvs unlink --namespace=$NAMESPACETEST -Rf $DIR &&
        ! flux kvs dir --namespace=$NAMESPACETEST $DIR
'

test_expect_success NO_CHAIN_LINT 'kvs: wait in new namespace works' '
        VERS=`flux kvs version --namespace=$NAMESPACETEST` &&
        VERS=$((VERS + 1))
        flux kvs wait --namespace=$NAMESPACETEST $VERS &
        kvswaitpid=$!
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.xxx=99
        test_expect_code 0 wait $kvswaitpid
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a key in new namespace works'  '
        flux kvs unlink --namespace=$NAMESPACETEST -Rf $DIR &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.watch=0 &&
        rm -f watch_out
        flux kvs watch --namespace=$NAMESPACETEST -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "0" watch_out
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.watch=1 &&
        wait $watchpid
cat >expected <<-EOF &&
0
1
EOF
        test_cmp watch_out expected
'

test_expect_success 'kvs: namespace remove non existing namespace silently passes' '
	flux kvs namespace remove $NAMESPACETMP
'

test_expect_success 'kvs: namespace remove works' '
	flux kvs namespace create $NAMESPACETMP-BASIC &&
        flux kvs put --namespace=$NAMESPACETMP-BASIC --json $DIR.tmp=1 &&
        test_kvs_key_namespace $NAMESPACETMP-BASIC $DIR.tmp 1 &&
	flux kvs namespace remove $NAMESPACETMP-BASIC &&
        ! flux kvs get --namespace=$NAMESPACETMP-BASIC --json $DIR.tmp
'

# A namespace create races against the namespace remove above, as we
# can't confirm if the namespace remove has garbage collected itself
# yet.  So we use namespace_create_loop() to iterate and try
# namespace create many times until it succeeds.
test_expect_success 'kvs: namespace can be re-created after remove' '
        namespace_create_loop $NAMESPACETMP-BASIC &&
        flux kvs put --namespace=$NAMESPACETMP-BASIC --json $DIR.recreate=1 &&
        test_kvs_key_namespace $NAMESPACETMP-BASIC $DIR.recreate 1 &&
	flux kvs namespace remove $NAMESPACETMP-BASIC &&
        ! flux kvs get --namespace=$NAMESPACETMP-BASIC --json $DIR.recreate
'

test_expect_success 'kvs: removed namespace not listed' '
        ! flux kvs namespace list | grep $NAMESPACETMP-BASIC
'

#
# Basic tests, data in new namespace available across ranks
#

test_expect_success 'kvs: put value in new namespace, available on other ranks' '
        flux kvs unlink --namespace=$NAMESPACETEST -Rf $DIR &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.all=1 &&
        VERS=`flux kvs version --namespace=$NAMESPACETEST` &&
        flux exec -n sh -c "flux kvs wait --namespace=$NAMESPACETEST ${VERS} && \
                         flux kvs get --namespace=$NAMESPACETEST $DIR.all"
'

test_expect_success 'kvs: unlink value in new namespace, does not exist all ranks' '
        flux kvs unlink --namespace=$NAMESPACETEST $DIR.all &&
        VERS=`flux kvs version --namespace=$NAMESPACETEST` &&
        flux exec -n sh -c "flux kvs wait --namespace=$NAMESPACETEST ${VERS} && \
                         ! flux kvs get --namespace=$NAMESPACETEST $DIR.all"
'

# namespace remove on other ranks can take time, so we loop via
# get_kvs_namespace_fails_all_ranks_loop()
test_expect_success 'kvs: namespace remove works, recognized on other ranks' '
	flux kvs namespace create $NAMESPACETMP-ALL &&
        flux kvs put --namespace=$NAMESPACETMP-ALL --json $DIR.all=1 &&
        VERS=`flux kvs version --namespace=$NAMESPACETMP-ALL` &&
        flux exec -n sh -c "flux kvs wait --namespace=$NAMESPACETMP-ALL ${VERS} && \
                         flux kvs get --namespace=$NAMESPACETMP-ALL $DIR.all" &&
	flux kvs namespace remove $NAMESPACETMP-ALL &&
        get_kvs_namespace_fails_all_ranks_loop $NAMESPACETMP-ALL $DIR.all
'

# A namespace create races against the namespace remove above, as we
# can't confirm if the namespace remove has garbage collected itself
# yet.  So we use namespace_create_loop() to iterate and try
# namespace create many times until it succeeds.
#
# After putting the new value, we still don't know if ranks > 0 have
# recognized the original remove.  So we will loop until the new
# namespace is recognized everywhere.  Note that we cannot use flux
# kvs wait, b/c the version may work against an old namespace.
test_expect_success 'kvs: namespace can be re-created after remove, recognized on other ranks' '
        namespace_create_loop $NAMESPACETMP-ALL &&
        flux kvs put --namespace=$NAMESPACETMP-ALL --json $DIR.recreate=1 &&
        get_kvs_namespace_all_ranks_loop $NAMESPACETMP-ALL $DIR.recreate &&
	flux kvs namespace remove $NAMESPACETMP-ALL
'

#
# Namespace specification priority
#

test_expect_success 'kvs: namespace order setup' '
	flux kvs namespace create $NAMESPACEORDER-1 &&
	flux kvs namespace create $NAMESPACEORDER-2 &&
        flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.ordertest=1 &&
        flux kvs put --namespace=$NAMESPACEORDER-1 $DIR.ordertest=2 &&
        flux kvs put --namespace=$NAMESPACEORDER-2 $DIR.ordertest=3 &&
        test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.ordertest 1 &&
        test_kvs_key_namespace $NAMESPACEORDER-1 $DIR.ordertest 2 &&
        test_kvs_key_namespace $NAMESPACEORDER-2 $DIR.ordertest 3
'

test_expect_success 'kvs: no namespace specified, lookup defaults to primary namespace' '
        test_kvs_key $DIR.ordertest 1
'

test_expect_success 'kvs: lookup - namespace specified in environment variable works' '
        export FLUX_KVS_NAMESPACE=$NAMESPACEORDER-1 &&
        test_kvs_key $DIR.ordertest 2 &&
        unset FLUX_KVS_NAMESPACE &&
        export FLUX_KVS_NAMESPACE=$NAMESPACEORDER-2 &&
        test_kvs_key $DIR.ordertest 3 &&
        unset FLUX_KVS_NAMESPACE
'

test_expect_success 'kvs: lookup - namespace specified in command line overrides environment variable' '
        export FLUX_KVS_NAMESPACE=$NAMESPACETMP-BAD &&
        test_kvs_key_namespace $NAMESPACEORDER-1 $DIR.ordertest 2 &&
        test_kvs_key_namespace $NAMESPACEORDER-2 $DIR.ordertest 3 &&
        unset FLUX_KVS_NAMESPACE
'

test_expect_success 'kvs: no namespace specified, put defaults to primary namespace' '
        flux kvs put $DIR.puttest=1 &&
        test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.puttest 1
'

test_expect_success 'kvs: put - namespace specified in environment variable works' '
        export FLUX_KVS_NAMESPACE=$NAMESPACEORDER-1 &&
        flux kvs put $DIR.puttest=2 &&
        unset FLUX_KVS_NAMESPACE &&
        test_kvs_key_namespace $NAMESPACEORDER-1 $DIR.puttest 2 &&
        export FLUX_KVS_NAMESPACE=$NAMESPACEORDER-2 &&
        flux kvs put $DIR.puttest=3 &&
        unset FLUX_KVS_NAMESPACE &&
        test_kvs_key_namespace $NAMESPACEORDER-2 $DIR.puttest 3
'

test_expect_success 'kvs: put - namespace specified in command line overrides environment variable' '
        export FLUX_KVS_NAMESPACE=$NAMESPACETMP-BAD &&
        flux kvs put --namespace=$NAMESPACEORDER-1 $DIR.puttest=4 &&
        flux kvs put --namespace=$NAMESPACEORDER-2 $DIR.puttest=5 &&
        unset FLUX_KVS_NAMESPACE &&
        test_kvs_key_namespace $NAMESPACEORDER-1 $DIR.puttest 4 &&
        test_kvs_key_namespace $NAMESPACEORDER-2 $DIR.puttest 5
'

#
# Namespace corner case tests
#

NAMESPACEBAD=namespacebad

test_expect_success 'kvs: namespace create on existing namespace fails' '
	! flux kvs namespace create $NAMESPACETEST
'

test_expect_success 'kvs: namespace create on existing namespace fails on rank 1' '
	! flux exec -n -r 1 sh -c "flux kvs namespace create $NAMESPACETEST"
'

test_expect_success 'kvs: get fails on invalid namespace' '
        ! flux kvs get --namespace=$NAMESPACEBAD --json $DIR.test
'

test_expect_success 'kvs: get fails on invalid namespace on rank 1' '
	! flux exec -n -r 1 sh -c "flux kvs get --namespace=$NAMESPACEBAD $DIR.test"
'

test_expect_success 'kvs: put fails on invalid namespace' '
	! flux kvs put --namespace=$NAMESPACEBAD --json $DIR.test=1
'

test_expect_success 'kvs: put fails on invalid namespace on rank 1' '
        ! flux exec -n -r 1 sh -c "flux kvs put --namespace=$NAMESPACEBAD $DIR.test=1"
'

test_expect_success 'kvs: version fails on invalid namespace' '
	! flux kvs version --namespace=$NAMESPACEBAD
'

test_expect_success 'kvs: version fails on invalid namespace on rank 1' '
	! flux exec -n -r 1 sh -c "flux kvs version --namespace=$NAMESPACEBAD"
'

test_expect_success NO_CHAIN_LINT 'kvs: wait fails on invalid namespace' '
	! flux kvs wait --namespace=$NAMESPACEBAD 1
'

test_expect_success 'kvs: wait fails on invalid namespace on rank 1' '
        ! flux exec -n -r 1 sh -c "flux kvs wait --namespace=$NAMESPACEBAD 1"
'

test_expect_success NO_CHAIN_LINT 'kvs: watch fails on invalid namespace' '
	! flux kvs watch --namespace=$NAMESPACEBAD -c 1 $DIR.test
'

test_expect_success 'kvs: watch fails on invalid namespace on rank 1' '
        ! flux exec -n -r 1 sh -c "flux kvs watch --namespace=$NAMESPACEBAD -c 1 $DIR.test"
'

# watch errors are output to stdout, so grep for "Operation not supported"
test_expect_success NO_CHAIN_LINT 'kvs: watch gets ENOTSUP when namespace is removed' '
        flux kvs namespace create $NAMESPACETMP-REMOVE-WATCH0 &&
        flux kvs put --namespace=$NAMESPACETMP-REMOVE-WATCH0 --json $DIR.watch=0 &&
        rm -f watch_out
        flux kvs watch --namespace=$NAMESPACETMP-REMOVE-WATCH0 -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "0" watch_out
        flux kvs namespace remove $NAMESPACETMP-REMOVE-WATCH0 &&
        wait $watchpid &&
        grep "Operation not supported" watch_out
'

# watch errors are output to stdout, so grep for "Operation not supported"
# stdbuf -oL necessary to ensure waitfile can succeed
test_expect_success NO_CHAIN_LINT 'kvs: watch on rank 1 gets ENOTSUP when namespace is removed' '
        flux kvs namespace create $NAMESPACETMP-REMOVE-WATCH1 &&
        flux kvs put --namespace=$NAMESPACETMP-REMOVE-WATCH1 --json $DIR.watch=0 &&
        VERS=`flux kvs version --namespace=$NAMESPACETMP-REMOVE-WATCH1` &&
        rm -f watch_out
        stdbuf -oL flux exec -n -r 1 sh -c "flux kvs wait --namespace=$NAMESPACETMP-REMOVE-WATCH1 ${VERS}; \
                                         flux kvs watch --namespace=$NAMESPACETMP-REMOVE-WATCH1 -o -c 1 $DIR.watch" > watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "0" watch_out &&
        flux kvs namespace remove $NAMESPACETMP-REMOVE-WATCH1 &&
        wait $watchpid &&
        grep "Operation not supported" watch_out
'

# When we call fence_namespace_remove, we know fence sent to server,
# but no way of knowing if server has accepted/processed fence.  To
# avoid racing in this test, we iterate on the fence stat until it is
# non-zero to know it's ready for this test.
test_expect_success NO_CHAIN_LINT 'kvs: incomplete fence gets ENOTSUP when namespace is removed' '
        flux kvs namespace create $NAMESPACETMP-REMOVE-FENCE0 &&
        rm -f fence_out
        ${FLUX_BUILD_DIR}/t/kvs/fence_namespace_remove $NAMESPACETMP-REMOVE-FENCE0 fence0 > fence_out &
        watchpid=$! &&
        wait_fencecount_nonzero 0 $NAMESPACETMP-REMOVE-FENCE0 &&
        flux kvs namespace remove $NAMESPACETMP-REMOVE-FENCE0 &&
        wait $watchpid &&
        grep "flux_future_get: Operation not supported" fence_out
'


# When we call fence_namespace_remove, we know fence sent to server,
# but no way of knowing if server has accepted/processed fence.  To
# avoid racing in this test, we iterate on the fence stat until it is
# non-zero to know it's ready for this test.
test_expect_success NO_CHAIN_LINT 'kvs: incomplete fence on rank 1 gets ENOTSUP when namespace is removed' '
        flux kvs namespace create $NAMESPACETMP-REMOVE-FENCE1 &&
        rm -f fence_out
        flux exec -n -r 1 sh -c "${FLUX_BUILD_DIR}/t/kvs/fence_namespace_remove $NAMESPACETMP-REMOVE-FENCE1 fence1" > fence_out &
        watchpid=$! &&
        wait_fencecount_nonzero 1 $NAMESPACETMP-REMOVE-FENCE1 &&
        flux kvs namespace remove $NAMESPACETMP-REMOVE-FENCE1 &&
        wait $watchpid &&
        grep "flux_future_get: Operation not supported" fence_out
'

#
# Basic tests - no pollution between namespaces
#

test_expect_success 'kvs: put/get in different namespaces works' '
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.test=1 &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.test=2 &&
        test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 1 &&
        test_kvs_key_namespace $NAMESPACETEST $DIR.test 2
'

test_expect_success 'kvs: unlink in different namespaces works' '
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.testA=1 &&
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.testB=1 &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.testA=2 &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.testB=2 &&
        flux kvs unlink --namespace=$PRIMARYNAMESPACE $DIR.testA &&
        flux kvs unlink --namespace=$NAMESPACETEST $DIR.testB &&
        test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.testB 1 &&
        test_kvs_key_namespace $NAMESPACETEST $DIR.testA 2 &&
        ! flux kvs get --namespace=$PRIMARYNAMESPACE --json $DIR.testA &&
        ! flux kvs get --namespace=$NAMESPACETEST --json $DIR.testB
'

test_expect_success 'kvs: dir in different namespace works' '
        flux kvs unlink --namespace=$PRIMARYNAMESPACE -Rf $DIR &&
        flux kvs unlink --namespace=$NAMESPACETEST -Rf $DIR &&
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.a=10 &&
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.b=11 &&
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.c=12 &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.a=13 &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.b=14 &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.c=15 &&
        flux kvs dir --namespace=$PRIMARYNAMESPACE $DIR | sort > primaryoutput &&
        flux kvs dir --namespace=$NAMESPACETEST $DIR | sort > testoutput &&
        cat >primaryexpected <<EOF &&
$DIR.a = 10
$DIR.b = 11
$DIR.c = 12
EOF
        cat >testexpected <<EOF &&
$DIR.a = 13
$DIR.b = 14
$DIR.c = 15
EOF
        test_cmp primaryexpected primaryoutput &&
        test_cmp testexpected testoutput
'

test_expect_success 'kvs: unlink dir in different namespaces works' '
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.subdirA.A=A &&
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.subdirA.B=B &&
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.subdirB.A=A &&
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.subdirB.A=B &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.subdirA.A=A &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.subdirA.B=B &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.subdirB.A=A &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.subdirB.A=B &&
        flux kvs unlink --namespace=$PRIMARYNAMESPACE -Rf $DIR.subdirA &&
        flux kvs unlink --namespace=$NAMESPACETEST -Rf $DIR.subdirB &&
        ! flux kvs dir --namespace=$PRIMARYNAMESPACE $DIR.subdirA &&
        flux kvs dir --namespace=$PRIMARYNAMESPACE $DIR.subdirB &&
        flux kvs dir --namespace=$NAMESPACETEST $DIR.subdirA &&
        ! flux kvs dir --namespace=$NAMESPACETEST $DIR.subdirB
'

test_expect_success NO_CHAIN_LINT 'kvs: wait in different namespaces works' '
        PRIMARYVERS=$(flux kvs version --namespace=$PRIMARYNAMESPACE)
        PRIMARYVERS=$((PRIMARYVERS + 1))
        flux kvs wait --namespace=$PRIMARYNAMESPACE $PRIMARYVERS &
        primarykvswaitpid=$!

        TESTVERS=$(flux kvs version --namespace=$NAMESPACETEST)
        TESTVERS=$((TESTVERS + 1))
        flux kvs wait --namespace=$NAMESPACETEST $TESTVERS &
        testkvswaitpid=$!

        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.xxx=X
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.xxx=X

        test_expect_code 0 wait $primarykvswaitpid
        test_expect_code 0 wait $testkvswaitpid
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a key in different namespaces works'  '
        flux kvs unlink --namespace=$PRIMARYNAMESPACE -Rf $DIR &&
        flux kvs unlink --namespace=$NAMESPACETEST -Rf $DIR &&
        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.watch=0 &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.watch=1 &&
        rm -f primary_watch_out
        rm -f test_watch_out

        flux kvs watch -o -c 1 $DIR.watch >primary_watch_out &
        primarywatchpid=$! &&
        $waitfile -q -t 5 -p "0" primary_watch_out

        flux kvs watch --namespace=$NAMESPACETEST -o -c 1 $DIR.watch >test_watch_out &
        testwatchpid=$! &&
        $waitfile -q -t 5 -p "1" test_watch_out

        flux kvs put --namespace=$PRIMARYNAMESPACE --json $DIR.watch=1 &&
        flux kvs put --namespace=$NAMESPACETEST --json $DIR.watch=2 &&
        wait $primarywatchpid &&
        wait $testwatchpid
cat >primaryexpected <<-EOF &&
0
1
EOF
cat >testexpected <<-EOF &&
1
2
EOF
        test_cmp primaryexpected primary_watch_out &&
        test_cmp testexpected test_watch_out
'

test_done
