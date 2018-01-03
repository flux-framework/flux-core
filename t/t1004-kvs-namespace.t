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

test_kvs_key() {
	flux kvs get --json "$1" >output
	echo "$2" >expected
	test_cmp expected output
}

test_kvs_key_namespace() {
        export FLUX_KVS_NAMESPACE=$1
	flux kvs get --json "$2" >output
	echo "$3" >expected
        unset FLUX_KVS_NAMESPACE
	test_cmp expected output
}

put_kvs_key_namespace() {
        export FLUX_KVS_NAMESPACE=$1
	flux kvs put --json "$2=$3"
        unset FLUX_KVS_NAMESPACE
}

dir_kvs_namespace() {
        export FLUX_KVS_NAMESPACE=$1
	flux kvs dir "$2" | sort > $3
        unset FLUX_KVS_NAMESPACE
}

unlink_kvs_namespace() {
        export FLUX_KVS_NAMESPACE=$1
	flux kvs unlink $2
        unset FLUX_KVS_NAMESPACE
}

unlink_kvs_dir_namespace() {
        export FLUX_KVS_NAMESPACE=$1
	flux kvs unlink -Rf $2
        unset FLUX_KVS_NAMESPACE
}

version_kvs_namespace() {
        export FLUX_KVS_NAMESPACE=$1
	version=`flux kvs version`
        eval $2=$version
        unset FLUX_KVS_NAMESPACE
}

get_kvs_namespace_exitvalue() {
        export FLUX_KVS_NAMESPACE=$1
	flux kvs get --json "$2"
        eval $3="$?"
        unset FLUX_KVS_NAMESPACE
}

dir_kvs_namespace_exitvalue() {
        export FLUX_KVS_NAMESPACE=$1
	flux kvs dir "$2"
        eval $3="$?"
        unset FLUX_KVS_NAMESPACE
}

namespace_create_loop() {
        i=0
        while ! flux kvs namespace-create $1 && [ $i -lt 50 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

get_kvs_namespace_all_ranks_loop() {
        i=0
        while ! flux exec sh -c "export FLUX_KVS_NAMESPACE=$1; flux kvs get $2" && [ $i -lt 50 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

get_kvs_namespace_fails_all_ranks_loop() {
        i=0
        while ! flux exec sh -c "export FLUX_KVS_NAMESPACE=$1; ! flux kvs get $2" && [ $i -lt 50 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

wait_watch_put_namespace() {
        export FLUX_KVS_NAMESPACE=$1
        i=0
        while [ "$(flux kvs get --json $2 2> /dev/null)" != "$3" ] && [ $i -lt 50 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        unset FLUX_KVS_NAMESPACE
        return $(loophandlereturn $i)
}

wait_fencecount_nonzero() {
        i=0
        while [ "$(flux exec -r $1 sh -c "flux module stats --parse namespace.$2.#fences kvs" 2> /dev/null)" == "0" ] && [ $i -lt 50 ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

#
# Basic tests in default primary namespace
#

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
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 2 &&
        test_kvs_key $DIR.test 2
'

test_expect_success 'kvs: put/get with primary namespace works' '
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 3 &&
        test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 3
'

test_expect_success 'kvs: unlink with primary namespace works' '
        unlink_kvs_namespace $PRIMARYNAMESPACE $DIR.test &&
        test_must_fail flux kvs get --json $DIR.test
'

test_expect_success 'kvs: dir with primary namespace works' '
        flux kvs put --json $DIR.a=1 &&
        flux kvs put --json $DIR.b=2 &&
        flux kvs put --json $DIR.c=3 &&
        dir_kvs_namespace $PRIMARYNAMESPACE $DIR output &&
        cat >expected <<EOF &&
$DIR.a = 1
$DIR.b = 2
$DIR.c = 3
EOF
        test_cmp expected output
'

test_expect_success 'kvs: unlink dir primary namespace works' '
        unlink_kvs_dir_namespace $PRIMARYNAMESPACE $DIR &&
        dir_kvs_namespace_exitvalue $PRIMARYNAMESPACE $DIR exitvalue &&
        test $exitvalue -ne 0
'

test_expect_success NO_CHAIN_LINT 'kvs: wait on primary namespace works' '
        VERS=$(flux kvs version)
        VERS=$((VERS + 1))
        export FLUX_KVS_NAMESPACE=$PRIMARYNAMESPACE
        flux kvs wait $VERS &
        kvswaitpid=$!
        flux kvs put --json $DIR.xxx=99
        unset FLUX_KVS_NAMESPACE
        test_expect_code 0 wait $kvswaitpid
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a key in primary namespace works'  '
        unlink_kvs_dir_namespace $PRIMARYNAMESPACE $DIR &&
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.watch 0 &&
        wait_watch_put_namespace $PRIMARYNAMESPACE "$DIR.watch" "0"
        rm -f watch_out
        export FLUX_KVS_NAMESPACE=$PRIMARYNAMESPACE
        stdbuf -oL flux kvs watch -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        wait_watch_file watch_out "0"
        flux kvs put --json $DIR.watch=1 &&
        wait $watchpid
        unset FLUX_KVS_NAMESPACE
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

test_expect_success 'kvs: namespace create on rank 1 works' '
	flux exec -r 1 sh -c "flux kvs namespace-create $NAMESPACERANK1"
'

test_expect_success 'kvs: put/get value in new namespace works' '
        put_kvs_key_namespace $NAMESPACETEST $DIR.test 1 &&
        test_kvs_key_namespace $NAMESPACETEST $DIR.test 1
'

test_expect_success 'kvs: unlink in new namespace works' '
        unlink_kvs_namespace $NAMESPACETEST $DIR.test &&
        get_kvs_namespace_exitvalue $NAMESPACETEST $DIR.test exitvalue &&
        test $exitvalue -ne 0
'

test_expect_success 'kvs: dir in new namespace works' '
        put_kvs_key_namespace $NAMESPACETEST $DIR.a 4 &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.b 5 &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.c 6 &&
        dir_kvs_namespace $NAMESPACETEST $DIR output &&
        cat >expected <<EOF &&
$DIR.a = 4
$DIR.b = 5
$DIR.c = 6
EOF
        test_cmp expected output
'

test_expect_success 'kvs: unlink dir in new namespace works' '
        unlink_kvs_dir_namespace $NAMESPACETEST $DIR &&
        dir_kvs_namespace_exitvalue $NAMESPACETEST $DIR exitvalue &&
        test $exitvalue -ne 0
'

test_expect_success NO_CHAIN_LINT 'kvs: wait in new namespace works' '
        version_kvs_namespace $NAMESPACETEST VERS
        VERS=$((VERS + 1))
        export FLUX_KVS_NAMESPACE=$NAMESPACETEST
        flux kvs wait $VERS &
        kvswaitpid=$!
        flux kvs put --json $DIR.xxx=99
        unset FLUX_KVS_NAMESPACE
        test_expect_code 0 wait $kvswaitpid
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a key in new namespace works'  '
        unlink_kvs_dir_namespace $NAMESPACETEST $DIR &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.watch 0 &&
        wait_watch_put_namespace $NAMESPACETEST "$DIR.watch" "0"
        rm -f watch_out
        export FLUX_KVS_NAMESPACE=$NAMESPACETEST
        stdbuf -oL flux kvs watch -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        wait_watch_file watch_out "0"
        flux kvs put --json $DIR.watch=1 &&
        wait $watchpid
        unset FLUX_KVS_NAMESPACE
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
        put_kvs_key_namespace $NAMESPACETMP-BASIC $DIR.tmp 1 &&
        test_kvs_key_namespace $NAMESPACETMP-BASIC $DIR.tmp 1 &&
	flux kvs namespace-remove $NAMESPACETMP-BASIC &&
        get_kvs_namespace_exitvalue $NAMESPACETMP-BASIC $DIR.tmp exitvalue &&
        test $exitvalue -ne 0
'

# A namespace-create races against the namespace-remove above, as we
# can't confirm if the namespace-remove has garbage collected itself
# yet.  So we use namespace_create_loop() to iterate and try
# namespace-create many times until it succeeds.
test_expect_success 'kvs: namespace can be re-created after remove' '
        namespace_create_loop $NAMESPACETMP-BASIC &&
        put_kvs_key_namespace $NAMESPACETMP-BASIC $DIR.recreate 1 &&
        test_kvs_key_namespace $NAMESPACETMP-BASIC $DIR.recreate 1 &&
	flux kvs namespace-remove $NAMESPACETMP-BASIC &&
        get_kvs_namespace_exitvalue $NAMESPACETMP-BASIC $DIR.recreate exitvalue &&
        test $exitvalue -ne 0
'

#
# Basic tests, data in new namespace available across ranks
#

test_expect_success 'kvs: put value in new namespace, available on other ranks' '
        unlink_kvs_dir_namespace $NAMESPACETEST $DIR &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.all 1 &&
        version_kvs_namespace $NAMESPACETEST VERS &&
        flux exec sh -c "export FLUX_KVS_NAMESPACE=$NAMESPACETEST; flux kvs wait ${VERS} && flux kvs get $DIR.all"
'

test_expect_success 'kvs: unlink value in new namespace, does not exist all ranks' '
        unlink_kvs_namespace $NAMESPACETEST $DIR.all &&
        version_kvs_namespace $NAMESPACETEST VERS &&
        flux exec sh -c "export FLUX_KVS_NAMESPACE=$NAMESPACETEST; flux kvs wait ${VERS} && ! flux kvs get $DIR.all"
'

# namespace-remove on other ranks can take time, so we loop via
# get_kvs_namespace_fails_all_ranks_loop()
test_expect_success 'kvs: namespace remove works, recognized on other ranks' '
	flux kvs namespace-create $NAMESPACETMP-ALL &&
        put_kvs_key_namespace $NAMESPACETMP-ALL $DIR.all 1 &&
        version_kvs_namespace $NAMESPACETMP-ALL VERS &&
        flux exec sh -c "export FLUX_KVS_NAMESPACE=$NAMESPACETMP-ALL; flux kvs wait ${VERS} && flux kvs get $DIR.all" &&
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
        put_kvs_key_namespace $NAMESPACETMP-ALL $DIR.recreate 1 &&
        get_kvs_namespace_all_ranks_loop $NAMESPACETMP-ALL $DIR.recreate &&
	flux kvs namespace-remove $NAMESPACETMP-ALL
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
        get_kvs_namespace_exitvalue $NAMESPACEBAD $DIR.test exitvalue &&
        test $exitvalue -ne 0
'

test_expect_success 'kvs: get fails on invalid namespace on rank 1' '
	! flux exec -r 1 sh -c "export FLUX_KVS_NAMESPACE=$NAMESPACEBAD ; flux kvs get $DIR.test"
'

test_expect_success NO_CHAIN_LINT 'kvs: put fails on invalid namespace' '
        export FLUX_KVS_NAMESPACE=$NAMESPACEBAD
	flux kvs put --json $DIR.test=1
        exitvalue=$?
        unset FLUX_KVS_NAMESPACE
        test $exitvalue -ne 0
'

test_expect_success 'kvs: put fails on invalid namespace on rank 1' '
        ! flux exec -r 1 sh -c "export FLUX_KVS_NAMESPACE=$NAMESPACEBAD ; flux kvs put $DIR.test=1"
'

test_expect_success NO_CHAIN_LINT 'kvs: version fails on invalid namespace' '
        export FLUX_KVS_NAMESPACE=$NAMESPACEBAD
	flux kvs version
        exitvalue=$?
        unset FLUX_KVS_NAMESPACE
        test $exitvalue -ne 0
'

test_expect_success 'kvs: version fails on invalid namespace on rank 1' '
	! flux exec -r 1 sh -c "export FLUX_KVS_NAMESPACE=$NAMESPACEBAD ; flux kvs version"
'

test_expect_success NO_CHAIN_LINT 'kvs: wait fails on invalid namespace' '
        export FLUX_KVS_NAMESPACE=$NAMESPACEBAD
	flux kvs wait 1
        exitvalue=$?
        unset FLUX_KVS_NAMESPACE
        test $exitvalue -ne 0
'

test_expect_success 'kvs: wait fails on invalid namespace on rank 1' '
        ! flux exec -r 1 sh -c "export FLUX_KVS_NAMESPACE=$NAMESPACEBAD ; flux kvs wait 1"
'

test_expect_success NO_CHAIN_LINT 'kvs: watch fails on invalid namespace' '
        export FLUX_KVS_NAMESPACE=$NAMESPACEBAD
	flux kvs watch -c 1 $DIR.test
        exitvalue=$?
        unset FLUX_KVS_NAMESPACE
        test $exitvalue -ne 0
'

test_expect_success 'kvs: watch fails on invalid namespace on rank 1' '
        ! flux exec -r 1 sh -c "export FLUX_KVS_NAMESPACE=$NAMESPACEBAD ; flux kvs watch -c 1 $DIR.test"
'

# watch errors are output to stdout, so grep for "Operation not supported"
test_expect_success NO_CHAIN_LINT 'kvs: watch gets ENOTSUP when namespace is removed' '
        flux kvs namespace-create $NAMESPACETMP-REMOVE-WATCH0 &&
        put_kvs_key_namespace $NAMESPACETMP-REMOVE-WATCH0 $DIR.watch 0 &&
        wait_watch_put_namespace $NAMESPACETMP-REMOVE-WATCH0 "$DIR.watch" "0"
        rm -f watch_out
        export FLUX_KVS_NAMESPACE=$NAMESPACETMP-REMOVE-WATCH0
        stdbuf -oL flux kvs watch -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        wait_watch_file watch_out "0"
        flux kvs namespace-remove $NAMESPACETMP-REMOVE-WATCH0 &&
        wait $watchpid &&
        unset FLUX_KVS_NAMESPACE
        grep "Operation not supported" watch_out
'

# watch errors are output to stdout, so grep for "Operation not supported"
test_expect_success NO_CHAIN_LINT 'kvs: watch on rank 1 gets ENOTSUP when namespace is removed' '
        flux kvs namespace-create $NAMESPACETMP-REMOVE-WATCH1 &&
        put_kvs_key_namespace $NAMESPACETMP-REMOVE-WATCH1 $DIR.watch 0 &&
        version_kvs_namespace $NAMESPACETMP-REMOVE-WATCH1 VERS &&
        rm -f watch_out
        stdbuf -oL flux exec -r 1 sh -c "export FLUX_KVS_NAMESPACE=$NAMESPACETMP-REMOVE-WATCH1 ; flux kvs wait ${VERS}; flux kvs watch -o -c 1 $DIR.watch" > watch_out &
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
        export FLUX_KVS_NAMESPACE=$NAMESPACETMP-REMOVE-FENCE0 &&
        rm -f fence_out
        stdbuf -oL ${FLUX_BUILD_DIR}/t/kvs/fence_namespace_remove fence0 > fence_out &
        watchpid=$! &&
        wait_fencecount_nonzero 0 $NAMESPACETMP-REMOVE-FENCE0 &&
        flux kvs namespace-remove $NAMESPACETMP-REMOVE-FENCE0 &&
        wait $watchpid &&
        unset FLUX_KVS_NAMESPACE &&
        grep "flux_future_get: Operation not supported" fence_out
'


# When we call fence_namespace_remove, we know fence sent to server,
# but no way of knowing if server has accepted/processed fence.  To
# avoid racing in this test, we iterate on the fence stat until it is
# non-zero to know it's ready for this test.
test_expect_success NO_CHAIN_LINT 'kvs: incomplete fence on rank 1 gets ENOTSUP when namespace is removed' '
        flux kvs namespace-create $NAMESPACETMP-REMOVE-FENCE1 &&
        rm -f fence_out
        stdbuf -oL flux exec -r 1 sh -c "export FLUX_KVS_NAMESPACE=$NAMESPACETMP-REMOVE-FENCE1 ; ${FLUX_BUILD_DIR}/t/kvs/fence_namespace_remove fence1" > fence_out &
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
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 1 &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.test 2 &&
        test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 1 &&
        test_kvs_key_namespace $NAMESPACETEST $DIR.test 2
'

test_expect_success 'kvs: unlink in different namespaces works' '
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.testA 1 &&
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.testB 1 &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.testA 2 &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.testB 2 &&
        unlink_kvs_namespace $PRIMARYNAMESPACE $DIR.testA &&
        unlink_kvs_namespace $NAMESPACETEST $DIR.testB &&
        test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.testB 1 &&
        test_kvs_key_namespace $NAMESPACETEST $DIR.testA 2 &&
        get_kvs_namespace_exitvalue $PRIMARYNAMESPACE $DIR.testA exitvalue &&
        test $exitvalue -ne 0 &&
        get_kvs_namespace_exitvalue $NAMESPACETEST $DIR.testB exitvalue &&
        test $exitvalue -ne 0
'

test_expect_success 'kvs: dir in different namespace works' '
        unlink_kvs_dir_namespace $PRIMARYNAMESPACE $DIR &&
        unlink_kvs_dir_namespace $NAMESPACETEST $DIR &&
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.a 10 &&
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.b 11 &&
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.c 12 &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.a 13 &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.b 14 &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.c 15 &&
        dir_kvs_namespace $PRIMARYNAMESPACE $DIR primaryoutput &&
        dir_kvs_namespace $NAMESPACETEST $DIR testoutput &&
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
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.subdirA.A A &&
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.subdirA.B B &&
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.subdirB.A A &&
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.subdirB.A B &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.subdirA.A A &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.subdirA.B B &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.subdirB.A A &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.subdirB.A B &&
        unlink_kvs_dir_namespace $PRIMARYNAMESPACE $DIR.subdirA &&
        unlink_kvs_dir_namespace $NAMESPACETEST $DIR.subdirB &&
        dir_kvs_namespace_exitvalue $PRIMARYNAMESPACE $DIR.subdirA exitvalue &&
        test $exitvalue -ne 0 &&
        dir_kvs_namespace_exitvalue $PRIMARYNAMESPACE $DIR.subdirB exitvalue &&
        test $exitvalue -eq 0 &&
        dir_kvs_namespace_exitvalue $NAMESPACETEST $DIR.subdirA exitvalue &&
        test $exitvalue -eq 0 &&
        dir_kvs_namespace_exitvalue $NAMESPACETEST $DIR.subdirB exitvalue &&
        test $exitvalue -ne 0
'

test_expect_success NO_CHAIN_LINT 'kvs: wait in different namespaces works' '
        export FLUX_KVS_NAMESPACE=$PRIMARYNAMESPACE
        PRIMARYVERS=$(flux kvs version)
        PRIMARYVERS=$((PRIMARYVERS + 1))
        flux kvs wait $PRIMARYVERS &
        primarykvswaitpid=$!
        unset FLUX_KVS_NAMESPACE

        export FLUX_KVS_NAMESPACE=$NAMESPACETEST
        TESTVERS=$(flux kvs version)
        TESTVERS=$((TESTVERS + 1))
        flux kvs wait $TESTVERS &
        testkvswaitpid=$!
        unset FLUX_KVS_NAMESPACE

        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.xxx X
        put_kvs_key_namespace $NAMESPACETEST $DIR.xxx X

        test_expect_code 0 wait $primarykvswaitpid
        test_expect_code 0 wait $testkvswaitpid
'

test_expect_success NO_CHAIN_LINT 'kvs: watch a key in different namespaces works'  '
        unlink_kvs_dir_namespace $PRIMARYNAMESPACE $DIR &&
        unlink_kvs_dir_namespace $NAMESPACETEST $DIR &&
        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.watch 0 &&
        wait_watch_put_namespace $PRIMARYNAMESPACE "$DIR.watch" "0"
        put_kvs_key_namespace $NAMESPACETEST $DIR.watch 1 &&
        wait_watch_put_namespace $NAMESPACETEST "$DIR.watch" "1"
        rm -f primary_watch_out
        rm -f test_watch_out

        stdbuf -oL flux kvs watch -o -c 1 $DIR.watch >primary_watch_out &
        primarywatchpid=$! &&
        wait_watch_file primary_watch_out "0"

        export FLUX_KVS_NAMESPACE=$NAMESPACETEST
        stdbuf -oL flux kvs watch -o -c 1 $DIR.watch >test_watch_out &
        testwatchpid=$! &&
        wait_watch_file test_watch_out "1"
        unset FLUX_KVS_NAMESPACE

        put_kvs_key_namespace $PRIMARYNAMESPACE $DIR.watch 1 &&
        put_kvs_key_namespace $NAMESPACETEST $DIR.watch 2 &&
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
