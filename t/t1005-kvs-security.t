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

# Just in case its set in the environment
unset FLUX_KVS_NAMESPACE

NAMESPACETMP=namespacetmp

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

put_kvs_namespace_exitvalue() {
        export FLUX_KVS_NAMESPACE=$1
	flux kvs put --json "$2=$3"
        eval $4="$?"
        unset FLUX_KVS_NAMESPACE
}

get_kvs_namespace_exitvalue() {
        export FLUX_KVS_NAMESPACE=$1
	flux kvs get --json "$2"
        eval $3="$?"
        unset FLUX_KVS_NAMESPACE
}

watch_kvs_namespace_exitvalue() {
        export FLUX_KVS_NAMESPACE=$1
	flux kvs watch -o -c 1 "$2"
        eval $3="$?"
        unset FLUX_KVS_NAMESPACE
}

version_kvs_namespace_exitvalue() {
        export FLUX_KVS_NAMESPACE=$1
	flux kvs version
        eval $2="$?"
        unset FLUX_KVS_NAMESPACE
}

wait_kvs_namespace_exitvalue() {
        export FLUX_KVS_NAMESPACE=$1
	flux kvs wait $2
        eval $3="$?"
        unset FLUX_KVS_NAMESPACE
}

wait_watch_put_namespace() {
        export FLUX_KVS_NAMESPACE=$1
        wait_watch_put $2 $3
        exitvalue=$?
        unset FLUX_KVS_NAMESPACE
        return $exitvalue
}

set_userid() {
        export FLUX_HANDLE_USERID=$1
        export FLUX_HANDLE_ROLEMASK=0x2
}

unset_userid() {
        unset FLUX_HANDLE_USERID
        unset FLUX_HANDLE_ROLEMASK
}

#
# Basic tests, make sure only instance owner can perform operations
# and non-namespace owner can't perform operations
#

test_expect_success 'kvs: namespace create fails (user)' '
        set_userid 9999 &&
	! flux kvs namespace-create $NAMESPACETMP-OWNER &&
        unset_userid
'

test_expect_success 'kvs: namespace create works (owner)' '
	flux kvs namespace-create $NAMESPACETMP-OWNER
'

test_expect_success 'kvs: put fails (user)' '
        set_userid 9999 &&
        put_kvs_namespace_exitvalue $NAMESPACETMP-OWNER $DIR.test 1 exitvalue &&
        unset_userid &&
        test $exitvalue -ne 0
'

test_expect_success 'kvs: put works (owner)' '
        put_kvs_key_namespace $NAMESPACETMP-OWNER $DIR.test 1 &&
        test_kvs_key_namespace $NAMESPACETMP-OWNER $DIR.test 1
'

test_expect_success 'kvs: get fails (user)' '
        set_userid 9999 &&
        get_kvs_namespace_exitvalue $NAMESPACETMP-OWNER $DIR.test exitvalue &&
        unset_userid &&
        test $exitvalue -ne 0
'

test_expect_success 'kvs: get fails on other ranks (user)' '
        ! flux exec -r 1 sh -c "FLUX_KVS_NAMESPACE=$NAMESPACETMP-OWNER \
                                FLUX_HANDLE_USERID=9999 \
                                FLUX_HANDLE_ROLEMASK=0x2 \
                                flux kvs get $DIR.test"
'

test_expect_success 'kvs: get works on other ranks (owner)' '
        flux exec -r 1 sh -c "FLUX_KVS_NAMESPACE=$NAMESPACETMP-OWNER \
                              flux kvs get $DIR.test"
'


test_expect_success NO_CHAIN_LINT 'kvs: watch works (owner)'  '
        unlink_kvs_dir_namespace $NAMESPACETMP-OWNER $DIR &&
        put_kvs_key_namespace $NAMESPACETMP-OWNER $DIR.watch 0 &&
        wait_watch_put_namespace $NAMESPACETMP-OWNER "$DIR.watch" "0"
        rm -f watch_out
        export FLUX_KVS_NAMESPACE=$NAMESPACETMP-OWNER
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

test_expect_success 'kvs: watch fails (user)' '
        set_userid 9999 &&
        watch_kvs_namespace_exitvalue $NAMESPACETMP-OWNER $DIR.test exitvalue &&
        unset_userid &&
        test $exitvalue -ne 0
'

test_expect_success 'kvs: version fails (user)' '
        set_userid 9999 &&
        version_kvs_namespace_exitvalue $NAMESPACETMP-OWNER exitvalue &&
        unset_userid &&
        test $exitvalue -ne 0
'

test_expect_success 'kvs: version fails on other ranks (user)' '
        ! flux exec -r 1 sh -c "FLUX_KVS_NAMESPACE=$NAMESPACETMP-OWNER \
                                FLUX_HANDLE_USERID=9999 \
                                FLUX_HANDLE_ROLEMASK=0x2 \
                                flux kvs version"
'

test_expect_success 'kvs: wait fails (user)' '
        set_userid 9999 &&
        wait_kvs_namespace_exitvalue $NAMESPACETMP-OWNER $DIR.test exitvalue &&
        unset_userid &&
        test $exitvalue -ne 0
'

test_expect_success 'kvs: namespace remove fails (user)' '
        set_userid 9999 &&
	! flux kvs namespace-remove $NAMESPACETMP-OWNER &&
        unset_userid
'

test_expect_success 'kvs: namespace remove works (owner)' '
        put_kvs_key_namespace $NAMESPACETMP-OWNER $DIR.tmp 1 &&
        test_kvs_key_namespace $NAMESPACETMP-OWNER $DIR.tmp 1 &&
        flux kvs namespace-remove $NAMESPACETMP-OWNER &&
        get_kvs_namespace_exitvalue $NAMESPACETMP-OWNER $DIR.tmp exitvalue &&
        test $exitvalue -ne 0
'

#
# Namespace tests owned by user
#

test_expect_success 'kvs: namespace create works (owner, for user)' '
	flux kvs namespace-create -o 9999 $NAMESPACETMP-USER
'

test_expect_success 'kvs: namespace put/get works (user)' '
        set_userid 9999 &&
        put_kvs_key_namespace $NAMESPACETMP-USER $DIR.test 1 &&
        test_kvs_key_namespace $NAMESPACETMP-USER $DIR.test 1 &&
        unset_userid
'

test_expect_success 'kvs: put/get works (owner)' '
        put_kvs_key_namespace $NAMESPACETMP-USER $DIR.test 2 &&
        test_kvs_key_namespace $NAMESPACETMP-USER $DIR.test 2
'

test_expect_success 'kvs: put fails (wrong user)' '
        set_userid 9000 &&
        put_kvs_namespace_exitvalue $NAMESPACETMP-USER $DIR.test 1 exitvalue &&
        unset_userid &&
        test $exitvalue -ne 0
'

test_expect_success 'kvs: get works on other ranks (user)' '
        flux exec -r 1 sh -c "FLUX_KVS_NAMESPACE=$NAMESPACETMP-USER \
                              FLUX_HANDLE_USERID=9999 \
                              FLUX_HANDLE_ROLEMASK=0x2 \
                              flux kvs get $DIR.test"
'

test_expect_success 'kvs: get works on other ranks (owner)' '
        flux exec -r 1 sh -c "FLUX_KVS_NAMESPACE=$NAMESPACETMP-USER \
                              flux kvs get $DIR.test"
'

test_expect_success 'kvs: get fails (wrong user)' '
        set_userid 9000 &&
        get_kvs_namespace_exitvalue $NAMESPACETMP-USER $DIR.test exitvalue &&
        unset_userid &&
        test $exitvalue -ne 0
'

test_expect_success 'kvs: get fails on other ranks (wrong user)' '
        ! flux exec -r 1 sh -c "FLUX_KVS_NAMESPACE=$NAMESPACETMP-USER \
                                FLUX_HANDLE_USERID=9000 \
                                FLUX_HANDLE_ROLEMASK=0x2 \
                                flux kvs get $DIR.test"
'

test_expect_success NO_CHAIN_LINT 'kvs: watch works (user)'  '
        set_userid 9999 &&
        unlink_kvs_dir_namespace $NAMESPACETMP-USER $DIR &&
        put_kvs_key_namespace $NAMESPACETMP-USER $DIR.watch 0 &&
        wait_watch_put_namespace $NAMESPACETMP-USER "$DIR.watch" "0"
        rm -f watch_out
        export FLUX_KVS_NAMESPACE=$NAMESPACETMP-USER
        stdbuf -oL flux kvs watch -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        wait_watch_file watch_out "0"
        flux kvs put --json $DIR.watch=1 &&
        wait $watchpid
        unset FLUX_KVS_NAMESPACE
        unset_userid
cat >expected <<-EOF &&
0
1
EOF
        test_cmp watch_out expected
'

test_expect_success NO_CHAIN_LINT 'kvs: watch works (owner)'  '
        unlink_kvs_dir_namespace $NAMESPACETMP-USER $DIR &&
        put_kvs_key_namespace $NAMESPACETMP-USER $DIR.watch 0 &&
        wait_watch_put_namespace $NAMESPACETMP-USER "$DIR.watch" "0"
        rm -f watch_out
        export FLUX_KVS_NAMESPACE=$NAMESPACETMP-USER
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

test_expect_success 'kvs: watch fails (wrong user)' '
        set_userid 9000 &&
        watch_kvs_namespace_exitvalue $NAMESPACETMP-USER $DIR.test exitvalue &&
        unset_userid &&
        test $exitvalue -ne 0
'

test_expect_success NO_CHAIN_LINT 'kvs: version & wait works (user)' '
        set_userid 9999
        version_kvs_namespace $NAMESPACETMP-USER VERS
        VERS=$((VERS + 1))
        export FLUX_KVS_NAMESPACE=$NAMESPACETMP-USER
        flux kvs wait $VERS &
        kvswaitpid=$!
        flux kvs put --json $DIR.xxx=99
        unset FLUX_KVS_NAMESPACE
        unset_userid
        test_expect_code 0 wait $kvswaitpid
'

test_expect_success 'kvs: version works on other ranks (user)' '
        flux exec -r 1 sh -c "FLUX_KVS_NAMESPACE=$NAMESPACETMP-USER \
                              FLUX_HANDLE_USERID=9999 \
                              FLUX_HANDLE_ROLEMASK=0x2 \
                              flux kvs version"
'

test_expect_success 'kvs: version works on other ranks (owner)' '
        flux exec -r 1 sh -c "FLUX_KVS_NAMESPACE=$NAMESPACETMP-USER \
                              flux kvs version"
'

test_expect_success 'kvs: version fails (wrong user)' '
        set_userid 9000 &&
        version_kvs_namespace_exitvalue $NAMESPACETMP-USER exitvalue &&
        unset_userid &&
        test $exitvalue -ne 0
'

test_expect_success 'kvs: version fails on other ranks (wrong user)' '
        ! flux exec -r 1 sh -c "FLUX_KVS_NAMESPACE=$NAMESPACETMP-USER \
                                FLUX_HANDLE_USERID=9000 \
                                FLUX_HANDLE_ROLEMASK=0x2 \
                                flux kvs version"
'

test_expect_success 'kvs: wait fails (wrong user)' '
        set_userid 9000 &&
        wait_kvs_namespace_exitvalue $NAMESPACETMP-USER $DIR.test exitvalue &&
        unset_userid &&
        test $exitvalue -ne 0
'

test_expect_success 'kvs: namespace remove still fails (user)' '
        set_userid 9999 &&
        ! flux kvs namespace-remove $NAMESPACETMP-USER &&
        unset_userid
'

test_expect_success 'kvs: namespace remove still works (owner)' '
        put_kvs_key_namespace $NAMESPACETMP-USER $DIR.tmp 1 &&
        test_kvs_key_namespace $NAMESPACETMP-USER $DIR.tmp 1 &&
        flux kvs namespace-remove $NAMESPACETMP-USER &&
        get_kvs_namespace_exitvalue $NAMESPACETMP-USER $DIR.tmp exitvalue &&
        test $exitvalue -ne 0
'

#
# Basic tests, user can't perform non-namespace operations
#

test_expect_success 'kvs: dropcache fails (user)' '
        set_userid 9999 &&
        ! flux kvs dropcache &&
        unset_userid
'

test_expect_success 'kvs: stats fails (user)' '
        set_userid 9999 &&
        ! flux module stats kvs &&
        unset_userid
'

test_expect_success 'kvs: stats clear fails (user)' '
        set_userid 9999 &&
        ! flux module stats -c kvs &&
        unset_userid
'

test_done
