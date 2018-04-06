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

# Just in case its set in the environment
unset FLUX_KVS_NAMESPACE

PRIMARYNAMESPACE=primary
NAMESPACETEST=namespacetest
NAMESPACETMP=namespacetmp
NAMESPACERANK1=namespacerank1
NAMESPACEORDER=namespaceorder
NAMESPACEPREFIX=namespaceprefix

namespace_create_loop() {
        i=0
        while ! flux kvs namespace-create $1 && [ $i -lt ${KVS_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

get_kvs_namespace_all_ranks_loop() {
        i=0
        while ! flux exec sh -c "flux kvs --namespace=$1 get $2" \
              && [ $i -lt ${KVS_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

get_kvs_namespace_fails_all_ranks_loop() {
        i=0
        while ! flux exec sh -c "! flux kvs --namespace=$1 get $2" \
              && [ $i -lt ${KVS_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

wait_fencecount_nonzero() {
        i=0
        while [ "$(flux exec -r $1 sh -c "flux module stats --parse namespace.$2.#transactions kvs" 2> /dev/null)" == "0" ] \
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
        flux kvs namespace-list | grep primary
'

test_expect_success 'kvs: create primary namespace fails' '
	! flux kvs namespace-create $PRIMARYNAMESPACE
'

test_expect_success 'kvs: remove primary namespace fails' '
	! flux kvs namespace-remove $PRIMARYNAMESPACE
'

test_expect_success 'kvs: get with primary namespace works' '
        flux kvs put --json $DIR.test=1 &&
        test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 1
'

test_expect_success 'kvs: put with primary namespace works' '
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.test=2 &&
        test_kvs_key $DIR.test 2
'

test_expect_success 'kvs: put/get with primary namespace works' '
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.test=3 &&
        test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 3
'

test_expect_success 'kvs: unlink with primary namespace works' '
        flux kvs --namespace=$PRIMARYNAMESPACE unlink $DIR.test &&
        test_must_fail flux kvs get --json $DIR.test
'

test_expect_success 'kvs: dir with primary namespace works' '
        flux kvs put --json $DIR.a=1 &&
        flux kvs put --json $DIR.b=2 &&
        flux kvs put --json $DIR.c=3 &&
        flux kvs --namespace=$PRIMARYNAMESPACE dir $DIR | sort > output &&
        cat >expected <<EOF &&
$DIR.a = 1
$DIR.b = 2
$DIR.c = 3
EOF
        test_cmp expected output
'

test_expect_success 'kvs: unlink dir primary namespace works' '
        flux kvs --namespace=$PRIMARYNAMESPACE unlink -Rf $DIR &&
        ! flux kvs --namespace=$PRIMARYNAMESPACE dir $DIR
'

test_expect_success NO_CHAIN_LINT 'kvs: wait on primary namespace works' '
        VERS=$(flux kvs version)
        VERS=$((VERS + 1))
        flux kvs --namespace=$PRIMARYNAMESPACE wait $VERS &
        kvswaitpid=$!
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.xxx=99
        test_expect_code 0 wait $kvswaitpid
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a key in primary namespace works'  '
        flux kvs --namespace=$PRIMARYNAMESPACE unlink -Rf $DIR &&
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.watch=0 &&
        wait_watch_put_namespace $PRIMARYNAMESPACE "$DIR.watch" "0"
        rm -f watch_out
        stdbuf -oL flux kvs --namespace=$PRIMARYNAMESPACE watch -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        wait_watch_file watch_out "0"
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.watch=1 &&
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
	flux kvs namespace-create $NAMESPACETEST
'

test_expect_success 'kvs: new namespace exists/is listed' '
        flux kvs namespace-list | grep $NAMESPACETEST
'

test_expect_success 'kvs: namespace create on rank 1 works' '
	flux exec -r 1 sh -c "flux kvs namespace-create $NAMESPACERANK1"
'

test_expect_success 'kvs: new namespace exists/is listed' '
        flux kvs namespace-list | grep $NAMESPACERANK1
'

test_expect_success 'kvs: put/get value in new namespace works' '
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.test=1 &&
        test_kvs_key_namespace $NAMESPACETEST $DIR.test 1
'

test_expect_success 'kvs: unlink in new namespace works' '
        flux kvs --namespace=$NAMESPACETEST unlink $DIR.test &&
        ! flux kvs --namespace=$NAMESPACETEST get --json $DIR.test
'

test_expect_success 'kvs: dir in new namespace works' '
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.a=4 &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.b=5 &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.c=6 &&
        flux kvs --namespace=$NAMESPACETEST dir $DIR | sort > output &&
        cat >expected <<EOF &&
$DIR.a = 4
$DIR.b = 5
$DIR.c = 6
EOF
        test_cmp expected output
'

test_expect_success 'kvs: unlink dir in new namespace works' '
        flux kvs --namespace=$NAMESPACETEST unlink -Rf $DIR &&
        ! flux kvs --namespace=$NAMESPACETEST dir $DIR
'

test_expect_success NO_CHAIN_LINT 'kvs: wait in new namespace works' '
        VERS=`flux kvs --namespace=$NAMESPACETEST version` &&
        VERS=$((VERS + 1))
        flux kvs --namespace=$NAMESPACETEST wait $VERS &
        kvswaitpid=$!
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.xxx=99
        test_expect_code 0 wait $kvswaitpid
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a key in new namespace works'  '
        flux kvs --namespace=$NAMESPACETEST unlink -Rf $DIR &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.watch=0 &&
        wait_watch_put_namespace $NAMESPACETEST "$DIR.watch" "0"
        rm -f watch_out
        stdbuf -oL flux kvs --namespace=$NAMESPACETEST watch -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        wait_watch_file watch_out "0"
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.watch=1 &&
        wait $watchpid
cat >expected <<-EOF &&
0
1
EOF
        test_cmp watch_out expected
'

test_expect_success 'kvs: namespace remove non existing namespace silently passes' '
	flux kvs namespace-remove $NAMESPACETMP
'

test_expect_success 'kvs: namespace remove works' '
	flux kvs namespace-create $NAMESPACETMP-BASIC &&
        flux kvs --namespace=$NAMESPACETMP-BASIC put --json $DIR.tmp=1 &&
        test_kvs_key_namespace $NAMESPACETMP-BASIC $DIR.tmp 1 &&
	flux kvs namespace-remove $NAMESPACETMP-BASIC &&
        ! flux kvs --namespace=$NAMESPACETMP-BASIC get --json $DIR.tmp
'

# A namespace-create races against the namespace-remove above, as we
# can't confirm if the namespace-remove has garbage collected itself
# yet.  So we use namespace_create_loop() to iterate and try
# namespace-create many times until it succeeds.
test_expect_success 'kvs: namespace can be re-created after remove' '
        namespace_create_loop $NAMESPACETMP-BASIC &&
        flux kvs --namespace=$NAMESPACETMP-BASIC put --json $DIR.recreate=1 &&
        test_kvs_key_namespace $NAMESPACETMP-BASIC $DIR.recreate 1 &&
	flux kvs namespace-remove $NAMESPACETMP-BASIC &&
        ! flux kvs --namespace=$NAMESPACETMP-BASIC get --json $DIR.recreate
'

test_expect_success 'kvs: removed namespace not listed' '
        ! flux kvs namespace-list | grep $NAMESPACETMP-BASIC
'

#
# Basic tests, data in new namespace available across ranks
#

test_expect_success 'kvs: put value in new namespace, available on other ranks' '
        flux kvs --namespace=$NAMESPACETEST unlink -Rf $DIR &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.all=1 &&
        VERS=`flux kvs --namespace=$NAMESPACETEST version` &&
        flux exec sh -c "flux kvs --namespace=$NAMESPACETEST wait ${VERS} && \
                         flux kvs --namespace=$NAMESPACETEST get $DIR.all"
'

test_expect_success 'kvs: unlink value in new namespace, does not exist all ranks' '
        flux kvs --namespace=$NAMESPACETEST unlink $DIR.all &&
        VERS=`flux kvs --namespace=$NAMESPACETEST version` &&
        flux exec sh -c "flux kvs --namespace=$NAMESPACETEST wait ${VERS} && \
                         ! flux kvs --namespace=$NAMESPACETEST get $DIR.all"
'

# namespace-remove on other ranks can take time, so we loop via
# get_kvs_namespace_fails_all_ranks_loop()
test_expect_success 'kvs: namespace remove works, recognized on other ranks' '
	flux kvs namespace-create $NAMESPACETMP-ALL &&
        flux kvs --namespace=$NAMESPACETMP-ALL put --json $DIR.all=1 &&
        VERS=`flux kvs --namespace=$NAMESPACETMP-ALL version` &&
        flux exec sh -c "flux kvs --namespace=$NAMESPACETMP-ALL wait ${VERS} && \
                         flux kvs --namespace=$NAMESPACETMP-ALL get $DIR.all" &&
	flux kvs namespace-remove $NAMESPACETMP-ALL &&
        get_kvs_namespace_fails_all_ranks_loop $NAMESPACETMP-ALL $DIR.all
'

# A namespace-create races against the namespace-remove above, as we
# can't confirm if the namespace-remove has garbage collected itself
# yet.  So we use namespace_create_loop() to iterate and try
# namespace-create many times until it succeeds.
#
# After putting the new value, we still don't know if ranks > 0 have
# recognized the original remove.  So we will loop until the new
# namespace is recognized everywhere.  Note that we cannot use flux
# kvs wait, b/c the version may work against an old namespace.
test_expect_success 'kvs: namespace can be re-created after remove, recognized on other ranks' '
        namespace_create_loop $NAMESPACETMP-ALL &&
        flux kvs --namespace=$NAMESPACETMP-ALL put --json $DIR.recreate=1 &&
        get_kvs_namespace_all_ranks_loop $NAMESPACETMP-ALL $DIR.recreate &&
	flux kvs namespace-remove $NAMESPACETMP-ALL
'

#
# Namespace specification priority
#

test_expect_success 'kvs: namespace order setup' '
	flux kvs namespace-create $NAMESPACEORDER-1 &&
	flux kvs namespace-create $NAMESPACEORDER-2 &&
        flux kvs --namespace=$PRIMARYNAMESPACE put $DIR.ordertest=1 &&
        flux kvs --namespace=$NAMESPACEORDER-1 put $DIR.ordertest=2 &&
        flux kvs --namespace=$NAMESPACEORDER-2 put $DIR.ordertest=3 &&
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

test_expect_success 'kvs: lookup - namespace specified in key overrides command line & environment variable' '
        export FLUX_KVS_NAMESPACE=$NAMESPACETMP-BAD &&
        test_kvs_key_namespace $NAMESPACEORDER-BAD ns:primary/$DIR.ordertest 1 &&
        test_kvs_key_namespace $NAMESPACEORDER-BAD ns:${NAMESPACEORDER}-1/$DIR.ordertest 2 &&
        test_kvs_key_namespace $NAMESPACEORDER-BAD ns:${NAMESPACEORDER}-2/$DIR.ordertest 3 &&
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
        flux kvs --namespace=$NAMESPACEORDER-1 put $DIR.puttest=4 &&
        flux kvs --namespace=$NAMESPACEORDER-2 put $DIR.puttest=5 &&
        unset FLUX_KVS_NAMESPACE &&
        test_kvs_key_namespace $NAMESPACEORDER-1 $DIR.puttest 4 &&
        test_kvs_key_namespace $NAMESPACEORDER-2 $DIR.puttest 5
'

test_expect_success 'kvs: put - namespace specified in key overrides command line & environment variable' '
        export FLUX_KVS_NAMESPACE=$NAMESPACETMP-BAD &&
        flux kvs --namespace=$NAMESPACEORDER-BAD put ns:${NAMESPACEORDER}-1/$DIR.puttest=6 &&
        flux kvs --namespace=$NAMESPACEORDER-BAD put ns:${NAMESPACEORDER}-2/$DIR.puttest=7 &&
        unset FLUX_KVS_NAMESPACE &&
        test_kvs_key_namespace ${NAMESPACEORDER}-1 $DIR.puttest 6 &&
        test_kvs_key_namespace ${NAMESPACEORDER}-2 $DIR.puttest 7
'

#
# Namespace prefix tests
#

test_expect_success 'kvs: namespace prefix setup' '
	flux kvs namespace-create $NAMESPACEPREFIX-1 &&
	flux kvs namespace-create $NAMESPACEPREFIX-2 &&
	flux kvs namespace-create $NAMESPACEPREFIX-watchprefix &&
        flux kvs --namespace=$NAMESPACEPREFIX-1 put $DIR.prefixtest=1 &&
        flux kvs --namespace=$NAMESPACEPREFIX-2 put $DIR.prefixtest=2 &&
        test_kvs_key_namespace $NAMESPACEPREFIX-1 $DIR.prefixtest 1 &&
        test_kvs_key_namespace $NAMESPACEPREFIX-2 $DIR.prefixtest 2
'

test_expect_success 'kvs: lookup - namespace chain fails' '
        ! flux kvs get ns:${NAMESPACEPREFIX}-1/ns:${NAMESPACEPREFIX}-2/$DIR.prefixtest
'

test_expect_success 'kvs: put - fails across multiple namespaces' '
        ! flux kvs put ns:${NAMESPACEPREFIX}-1/$DIR.puttest.a=1 ns:${NAMESPACEPREFIX}-2/$DIR.puttest.b=2
'

test_expect_success 'kvs: namespace prefix works with ls' '
        flux kvs ls ns:${NAMESPACEPREFIX}-1/. | sort >output &&
        cat >expected <<EOF &&
test
EOF
        test_cmp expected output
'

# Note double period, will be resolved in issue #1391 fix
test_expect_success 'kvs: namespace prefix works with dir' '
        flux kvs dir ns:${NAMESPACEPREFIX}-1/. | sort >output &&
        cat >expected <<EOF &&
ns:${NAMESPACEPREFIX}-1/..test.
EOF
        test_cmp expected output
'

test_expect_success 'kvs: namespace prefix works with ls' '
        flux kvs ls ns:${NAMESPACEPREFIX}-1/. | sort >output &&
        cat >expected <<EOF &&
test
EOF
        test_cmp expected output
'

test_expect_success 'kvs: namespace prefix with key suffix fails' '
        ! flux kvs get ns:${NAMESPACEPREFIX}-1/ &&
        ! flux kvs put ns:${NAMESPACEPREFIX}-1/ &&
        ! flux kvs dir ns:${NAMESPACEPREFIX}-1/ &&
        ! flux kvs ls ns:${NAMESPACEPREFIX}-1/ &&
        ! flux kvs watch ns:${NAMESPACEPREFIX}-1/
'

test_expect_success 'kvs: namespace prefix works across symlinks' '
        flux kvs put ns:${NAMESPACEPREFIX}-1/$DIR.linktest=1 &&
        flux kvs put ns:${NAMESPACEPREFIX}-2/$DIR.linktest=2 &&
        flux kvs link ns:${NAMESPACEPREFIX}-2/$DIR.linktest ns:${NAMESPACEPREFIX}-1/$DIR.link &&
        test_kvs_key_namespace ${NAMESPACEPREFIX}-1 $DIR.link 2
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a key with namespace prefix'  '
        flux kvs --namespace=${NAMESPACEPREFIX}-watchprefix unlink -Rf $DIR &&
        flux kvs --namespace=${NAMESPACEPREFIX}-watchprefix put --json $DIR.watch=0 &&
        wait_watch_put_namespace ${NAMESPACEPREFIX}-watchprefix "$DIR.watch" "0"
        rm -f watch_out
        stdbuf -oL flux kvs watch -o -c 1 ns:${NAMESPACEPREFIX}-watchprefix/$DIR.watch >watch_out &
        watchpid=$! &&
        wait_watch_file watch_out "0"
        flux kvs --namespace=${NAMESPACEPREFIX}-watchprefix put --json $DIR.watch=1 &&
        wait $watchpid
cat >expected <<-EOF &&
0
1
EOF
        test_cmp watch_out expected
'

#
# Namespace corner case tests
#

NAMESPACEBAD=namespacebad

test_expect_success 'kvs: namespace create on existing namespace fails' '
	! flux kvs namespace-create $NAMESPACETEST
'

test_expect_success 'kvs: namespace create on existing namespace fails on rank 1' '
	! flux exec -r 1 sh -c "flux kvs namespace-create $NAMESPACETEST"
'

test_expect_success 'kvs: get fails on invalid namespace' '
        ! flux kvs --namespace=$NAMESPACEBAD get --json $DIR.test
'

test_expect_success 'kvs: get fails on invalid namespace on rank 1' '
	! flux exec -r 1 sh -c "flux kvs --namespace=$NAMESPACEBAD get $DIR.test"
'

test_expect_success 'kvs: put fails on invalid namespace' '
	! flux kvs --namespace=$NAMESPACEBAD put --json $DIR.test=1
'

test_expect_success 'kvs: put fails on invalid namespace on rank 1' '
        ! flux exec -r 1 sh -c "flux kvs --namespace=$NAMESPACEBAD put $DIR.test=1"
'

test_expect_success 'kvs: version fails on invalid namespace' '
	! flux kvs --namespace=$NAMESPACEBAD version
'

test_expect_success 'kvs: version fails on invalid namespace on rank 1' '
	! flux exec -r 1 sh -c "flux kvs --namespace=$NAMESPACEBAD version"
'

test_expect_success NO_CHAIN_LINT 'kvs: wait fails on invalid namespace' '
	! flux kvs --namespace=$NAMESPACEBAD wait 1
'

test_expect_success 'kvs: wait fails on invalid namespace on rank 1' '
        ! flux exec -r 1 sh -c "flux kvs --namespace=$NAMESPACEBAD wait 1"
'

test_expect_success NO_CHAIN_LINT 'kvs: watch fails on invalid namespace' '
	! flux kvs --namespace=$NAMESPACEBAD watch -c 1 $DIR.test
'

test_expect_success 'kvs: watch fails on invalid namespace on rank 1' '
        ! flux exec -r 1 sh -c "flux kvs --namespace=$NAMESPACEBAD watch -c 1 $DIR.test"
'

# watch errors are output to stdout, so grep for "Operation not supported"
test_expect_success NO_CHAIN_LINT 'kvs: watch gets ENOTSUP when namespace is removed' '
        flux kvs namespace-create $NAMESPACETMP-REMOVE-WATCH0 &&
        flux kvs --namespace=$NAMESPACETMP-REMOVE-WATCH0 put --json $DIR.watch=0 &&
        wait_watch_put_namespace $NAMESPACETMP-REMOVE-WATCH0 "$DIR.watch" "0"
        rm -f watch_out
        stdbuf -oL flux kvs --namespace=$NAMESPACETMP-REMOVE-WATCH0 watch -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        wait_watch_file watch_out "0"
        flux kvs namespace-remove $NAMESPACETMP-REMOVE-WATCH0 &&
        wait $watchpid &&
        grep "Operation not supported" watch_out
'

# watch errors are output to stdout, so grep for "Operation not supported"
test_expect_success NO_CHAIN_LINT 'kvs: watch on rank 1 gets ENOTSUP when namespace is removed' '
        flux kvs namespace-create $NAMESPACETMP-REMOVE-WATCH1 &&
        flux kvs --namespace=$NAMESPACETMP-REMOVE-WATCH1 put --json $DIR.watch=0 &&
        VERS=`flux kvs --namespace=$NAMESPACETMP-REMOVE-WATCH1 version` &&
        rm -f watch_out
        stdbuf -oL flux exec -r 1 sh -c "flux kvs --namespace=$NAMESPACETMP-REMOVE-WATCH1 wait ${VERS}; \
                                         flux kvs --namespace=$NAMESPACETMP-REMOVE-WATCH1 watch -o -c 1 $DIR.watch" > watch_out &
        watchpid=$! &&
        wait_watch_file watch_out "0" &&
        flux kvs namespace-remove $NAMESPACETMP-REMOVE-WATCH1 &&
        wait $watchpid &&
        grep "Operation not supported" watch_out
'

# When we call fence_namespace_remove, we know fence sent to server,
# but no way of knowing if server has accepted/processed fence.  To
# avoid racing in this test, we iterate on the fence stat until it is
# non-zero to know it's ready for this test.
test_expect_success NO_CHAIN_LINT 'kvs: incomplete fence gets ENOTSUP when namespace is removed' '
        flux kvs namespace-create $NAMESPACETMP-REMOVE-FENCE0 &&
        rm -f fence_out
        stdbuf -oL ${FLUX_BUILD_DIR}/t/kvs/fence_namespace_remove $NAMESPACETMP-REMOVE-FENCE0 fence0 > fence_out &
        watchpid=$! &&
        wait_fencecount_nonzero 0 $NAMESPACETMP-REMOVE-FENCE0 &&
        flux kvs namespace-remove $NAMESPACETMP-REMOVE-FENCE0 &&
        wait $watchpid &&
        grep "flux_future_get: Operation not supported" fence_out
'


# When we call fence_namespace_remove, we know fence sent to server,
# but no way of knowing if server has accepted/processed fence.  To
# avoid racing in this test, we iterate on the fence stat until it is
# non-zero to know it's ready for this test.
test_expect_success NO_CHAIN_LINT 'kvs: incomplete fence on rank 1 gets ENOTSUP when namespace is removed' '
        flux kvs namespace-create $NAMESPACETMP-REMOVE-FENCE1 &&
        rm -f fence_out
        stdbuf -oL flux exec -r 1 sh -c "${FLUX_BUILD_DIR}/t/kvs/fence_namespace_remove $NAMESPACETMP-REMOVE-FENCE1 fence1" > fence_out &
        watchpid=$! &&
        wait_fencecount_nonzero 1 $NAMESPACETMP-REMOVE-FENCE1 &&
        flux kvs namespace-remove $NAMESPACETMP-REMOVE-FENCE1 &&
        wait $watchpid &&
        grep "flux_future_get: Operation not supported" fence_out
'

#
# Basic tests - no pollution between namespaces
#

test_expect_success 'kvs: put/get in different namespaces works' '
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.test=1 &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.test=2 &&
        test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 1 &&
        test_kvs_key_namespace $NAMESPACETEST $DIR.test 2
'

test_expect_success 'kvs: unlink in different namespaces works' '
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.testA=1 &&
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.testB=1 &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.testA=2 &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.testB=2 &&
        flux kvs --namespace=$PRIMARYNAMESPACE unlink $DIR.testA &&
        flux kvs --namespace=$NAMESPACETEST unlink $DIR.testB &&
        test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.testB 1 &&
        test_kvs_key_namespace $NAMESPACETEST $DIR.testA 2 &&
        ! flux kvs --namespace=$PRIMARYNAMESPACE get --json $DIR.testA &&
        ! flux kvs --namespace=$NAMESPACETEST get --json $DIR.testB
'

test_expect_success 'kvs: dir in different namespace works' '
        flux kvs --namespace=$PRIMARYNAMESPACE unlink -Rf $DIR &&
        flux kvs --namespace=$NAMESPACETEST unlink -Rf $DIR &&
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.a=10 &&
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.b=11 &&
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.c=12 &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.a=13 &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.b=14 &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.c=15 &&
        flux kvs --namespace=$PRIMARYNAMESPACE dir $DIR | sort > primaryoutput &&
        flux kvs --namespace=$NAMESPACETEST dir $DIR | sort > testoutput &&
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
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.subdirA.A=A &&
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.subdirA.B=B &&
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.subdirB.A=A &&
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.subdirB.A=B &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.subdirA.A=A &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.subdirA.B=B &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.subdirB.A=A &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.subdirB.A=B &&
        flux kvs --namespace=$PRIMARYNAMESPACE unlink -Rf $DIR.subdirA &&
        flux kvs --namespace=$NAMESPACETEST unlink -Rf $DIR.subdirB &&
        ! flux kvs --namespace=$PRIMARYNAMESPACE dir $DIR.subdirA &&
        flux kvs --namespace=$PRIMARYNAMESPACE dir $DIR.subdirB &&
        flux kvs --namespace=$NAMESPACETEST dir $DIR.subdirA &&
        ! flux kvs --namespace=$NAMESPACETEST dir $DIR.subdirB
'

test_expect_success NO_CHAIN_LINT 'kvs: wait in different namespaces works' '
        PRIMARYVERS=$(flux kvs --namespace=$PRIMARYNAMESPACE version)
        PRIMARYVERS=$((PRIMARYVERS + 1))
        flux kvs --namespace=$PRIMARYNAMESPACE wait $PRIMARYVERS &
        primarykvswaitpid=$!

        TESTVERS=$(flux kvs --namespace=$NAMESPACETEST version)
        TESTVERS=$((TESTVERS + 1))
        flux kvs --namespace=$NAMESPACETEST wait $TESTVERS &
        testkvswaitpid=$!

        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.xxx=X
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.xxx=X

        test_expect_code 0 wait $primarykvswaitpid
        test_expect_code 0 wait $testkvswaitpid
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a key in different namespaces works'  '
        flux kvs --namespace=$PRIMARYNAMESPACE unlink -Rf $DIR &&
        flux kvs --namespace=$NAMESPACETEST unlink -Rf $DIR &&
        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.watch=0 &&
        wait_watch_put_namespace $PRIMARYNAMESPACE "$DIR.watch" "0"
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.watch=1 &&
        wait_watch_put_namespace $NAMESPACETEST "$DIR.watch" "1"
        rm -f primary_watch_out
        rm -f test_watch_out

        stdbuf -oL flux kvs watch -o -c 1 $DIR.watch >primary_watch_out &
        primarywatchpid=$! &&
        wait_watch_file primary_watch_out "0"

        stdbuf -oL flux kvs --namespace=$NAMESPACETEST watch -o -c 1 $DIR.watch >test_watch_out &
        testwatchpid=$! &&
        wait_watch_file test_watch_out "1"

        flux kvs --namespace=$PRIMARYNAMESPACE put --json $DIR.watch=1 &&
        flux kvs --namespace=$NAMESPACETEST put --json $DIR.watch=2 &&
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
