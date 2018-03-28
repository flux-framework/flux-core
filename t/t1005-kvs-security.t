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
      	! flux kvs --namespace=$NAMESPACETMP-OWNER put --json $DIR.test=1 &&
        unset_userid
'

test_expect_success 'kvs: put works (owner)' '
        flux kvs --namespace=$NAMESPACETMP-OWNER put --json $DIR.test=1 &&
        test_kvs_key_namespace $NAMESPACETMP-OWNER $DIR.test 1
'

test_expect_success 'kvs: get fails (user)' '
        set_userid 9999 &&
	! flux kvs --namespace=$NAMESPACETMP-OWNER get --json $DIR.test &&
        unset_userid
'

test_expect_success 'kvs: get fails on other ranks (user)' '
        ! flux exec -r 1 sh -c "FLUX_HANDLE_USERID=9999 \
                                FLUX_HANDLE_ROLEMASK=0x2 \
                                flux kvs --namespace=$NAMESPACETMP-OWNER get $DIR.test"
'

test_expect_success 'kvs: get works on other ranks (owner)' '
        flux exec -r 1 sh -c "flux kvs --namespace=$NAMESPACETMP-OWNER get $DIR.test"
'


test_expect_success NO_CHAIN_LINT 'kvs: watch works (owner)'  '
        flux kvs --namespace=$NAMESPACETMP-OWNER unlink -Rf $DIR &&
        flux kvs --namespace=$NAMESPACETMP-OWNER put --json $DIR.watch=0 &&
        wait_watch_put_namespace $NAMESPACETMP-OWNER "$DIR.watch" "0"
        rm -f watch_out
        stdbuf -oL flux kvs --namespace=$NAMESPACETMP-OWNER watch -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        wait_watch_file watch_out "0"
        flux kvs --namespace=$NAMESPACETMP-OWNER put --json $DIR.watch=1 &&
        wait $watchpid
cat >expected <<-EOF &&
0
1
EOF
        test_cmp watch_out expected
'

test_expect_success 'kvs: watch fails (user)' '
        set_userid 9999 &&
        ! flux kvs --namespace=$NAMESPACETMP-OWNER watch -o -c 1 $DIR.test &&
        unset_userid
'

test_expect_success 'kvs: version fails (user)' '
        set_userid 9999 &&
        ! flux kvs --namespace=$NAMESPACETMP-OWNER version &&
        unset_userid
'

test_expect_success 'kvs: version fails on other ranks (user)' '
        ! flux exec -r 1 sh -c "FLUX_HANDLE_USERID=9999 \
                                FLUX_HANDLE_ROLEMASK=0x2 \
                                flux kvs --namespace=$NAMESPACETMP-OWNER version"
'

test_expect_success 'kvs: wait fails (user)' '
        set_userid 9999 &&
        ! flux kvs --namespace=$NAMESPACETMP-OWNER wait $DIR.test &&
        unset_userid
'

test_expect_success 'kvs: namespace remove fails (user)' '
        set_userid 9999 &&
	! flux kvs namespace-remove $NAMESPACETMP-OWNER &&
        unset_userid
'

test_expect_success 'kvs: namespace remove works (owner)' '
        flux kvs --namespace=$NAMESPACETMP-OWNER put --json $DIR.tmp=1 &&
        test_kvs_key_namespace $NAMESPACETMP-OWNER $DIR.tmp 1 &&
        flux kvs namespace-remove $NAMESPACETMP-OWNER &&
        ! flux kvs --namespace=$NAMESPACETMP-OWNER get --json $DIR.tmp
'

#
# Namespace tests owned by user
#

test_expect_success 'kvs: namespace create works (owner, for user)' '
	flux kvs namespace-create -o 9999 $NAMESPACETMP-USER
'

test_expect_success 'kvs: namespace listed with correct owner' '
        flux kvs namespace-list | grep $NAMESPACETMP-USER | grep 9999
'

test_expect_success 'kvs: namespace put/get works (user)' '
        set_userid 9999 &&
        flux kvs --namespace=$NAMESPACETMP-USER put --json $DIR.test=1 &&
        test_kvs_key_namespace $NAMESPACETMP-USER $DIR.test 1 &&
        unset_userid
'

test_expect_success 'kvs: put/get works (owner)' '
        flux kvs --namespace=$NAMESPACETMP-USER put --json $DIR.test=2 &&
        test_kvs_key_namespace $NAMESPACETMP-USER $DIR.test 2
'

test_expect_success 'kvs: put fails (wrong user)' '
        set_userid 9000 &&
      	! flux kvs --namespace=$NAMESPACETMP-USER put --json $DIR.test=1 &&
        unset_userid
'

test_expect_success 'kvs: get works on other ranks (user)' '
        flux exec -r 1 sh -c "FLUX_HANDLE_USERID=9999 \
                              FLUX_HANDLE_ROLEMASK=0x2 \
                              flux kvs --namespace=$NAMESPACETMP-USER get $DIR.test"
'

test_expect_success 'kvs: get works on other ranks (owner)' '
        flux exec -r 1 sh -c "flux kvs --namespace=$NAMESPACETMP-USER get $DIR.test"
'

test_expect_success 'kvs: get fails (wrong user)' '
        set_userid 9000 &&
        ! flux kvs --namespace=$NAMESPACETMP-USER get --json $DIR.test &&
        unset_userid
'

test_expect_success 'kvs: get fails on other ranks (wrong user)' '
        ! flux exec -r 1 sh -c "FLUX_HANDLE_USERID=9000 \
                                FLUX_HANDLE_ROLEMASK=0x2 \
                                flux kvs --namespace=$NAMESPACETMP-USER get $DIR.test"
'

test_expect_success 'kvs: get fails (wrong user, with at reference)' '
        set_userid 9999 &&
        ref=`flux kvs --namespace=$NAMESPACETMP-USER get --treeobj .` &&
        unset_userid &&
        set_userid 9000 &&
      	! flux kvs --namespace=$NAMESPACETMP-USER get --at ${ref} --json $DIR.test &&
        unset_userid
'

test_expect_success NO_CHAIN_LINT 'kvs: watch works (user)'  '
        set_userid 9999 &&
        flux kvs --namespace=$NAMESPACETMP-USER unlink -Rf $DIR &&
        flux kvs --namespace=$NAMESPACETMP-USER put --json $DIR.watch=0 &&
        wait_watch_put_namespace $NAMESPACETMP-USER "$DIR.watch" "0"
        rm -f watch_out
        stdbuf -oL flux kvs --namespace=$NAMESPACETMP-USER watch -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        wait_watch_file watch_out "0"
        flux kvs --namespace=$NAMESPACETMP-USER put --json $DIR.watch=1 &&
        wait $watchpid
        unset_userid
cat >expected <<-EOF &&
0
1
EOF
        test_cmp watch_out expected
'

test_expect_success NO_CHAIN_LINT 'kvs: watch works (owner)'  '
        flux kvs --namespace=$NAMESPACETMP-USER unlink -Rf $DIR &&
        flux kvs --namespace=$NAMESPACETMP-USER put --json $DIR.watch=0 &&
        wait_watch_put_namespace $NAMESPACETMP-USER "$DIR.watch" "0"
        rm -f watch_out
        stdbuf -oL flux kvs --namespace=$NAMESPACETMP-USER watch -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        wait_watch_file watch_out "0"
        flux kvs --namespace=$NAMESPACETMP-USER put --json $DIR.watch=1 &&
        wait $watchpid
cat >expected <<-EOF &&
0
1
EOF
        test_cmp watch_out expected
'

test_expect_success 'kvs: watch fails (wrong user)' '
        set_userid 9000 &&
        ! flux kvs --namespace=$NAMESPACETMP-USER watch -o -c 1 $DIR.test &&
        unset_userid
'

test_expect_success NO_CHAIN_LINT 'kvs: version & wait works (user)' '
        set_userid 9999 &&
        VERS=`flux kvs --namespace=$NAMESPACETMP-USER version` &&
        VERS=$((VERS + 1))
        flux kvs --namespace=$NAMESPACETMP-USER wait $VERS &
        kvswaitpid=$!
        flux kvs --namespace=$NAMESPACETMP-USER put --json $DIR.xxx=99
        unset_userid
        test_expect_code 0 wait $kvswaitpid
'

test_expect_success 'kvs: version works on other ranks (user)' '
        flux exec -r 1 sh -c "FLUX_HANDLE_USERID=9999 \
                              FLUX_HANDLE_ROLEMASK=0x2 \
                              flux kvs --namespace=$NAMESPACETMP-USER version"
'

test_expect_success 'kvs: version works on other ranks (owner)' '
        flux exec -r 1 sh -c "flux kvs --namespace=$NAMESPACETMP-USER version"
'

test_expect_success 'kvs: version fails (wrong user)' '
        set_userid 9000 &&
        ! flux kvs --namespace=$NAMESPACETMP-USER version &&
        unset_userid
'

test_expect_success 'kvs: version fails on other ranks (wrong user)' '
        ! flux exec -r 1 sh -c "FLUX_HANDLE_USERID=9000 \
                                FLUX_HANDLE_ROLEMASK=0x2 \
                                flux kvs --namespace=$NAMESPACETMP-USER version"
'

test_expect_success 'kvs: wait fails (wrong user)' '
        set_userid 9000 &&
        ! flux kvs --namespace=$NAMESPACETMP-USER wait $DIR.test &&
        unset_userid
'

test_expect_success 'kvs: namespace remove still fails (user)' '
        set_userid 9999 &&
        ! flux kvs namespace-remove $NAMESPACETMP-USER &&
        unset_userid
'

test_expect_success 'kvs: namespace remove still works (owner)' '
        flux kvs --namespace=$NAMESPACETMP-USER put --json $DIR.tmp=1 &&
        test_kvs_key_namespace $NAMESPACETMP-USER $DIR.tmp 1 &&
        flux kvs namespace-remove $NAMESPACETMP-USER &&
        ! flux kvs --namespace=$NAMESPACETMP-USER get --json $DIR.tmp
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
