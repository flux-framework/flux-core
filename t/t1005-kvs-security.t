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

waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

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
      	! flux kvs put --namespace=$NAMESPACETMP-OWNER --json $DIR.test=1 &&
        unset_userid
'

test_expect_success 'kvs: put works (owner)' '
        flux kvs put --namespace=$NAMESPACETMP-OWNER --json $DIR.test=1 &&
        test_kvs_key_namespace $NAMESPACETMP-OWNER $DIR.test 1
'

test_expect_success 'kvs: get fails (user)' '
        set_userid 9999 &&
	! flux kvs get --namespace=$NAMESPACETMP-OWNER --json $DIR.test &&
        unset_userid
'

test_expect_success 'kvs: get fails on other ranks (user)' '
        ! flux exec -n -r 1 sh -c "FLUX_HANDLE_USERID=9999 \
                                FLUX_HANDLE_ROLEMASK=0x2 \
                                flux kvs get --namespace=$NAMESPACETMP-OWNER $DIR.test"
'

test_expect_success 'kvs: get works on other ranks (owner)' '
        flux exec -n -r 1 sh -c "flux kvs get --namespace=$NAMESPACETMP-OWNER $DIR.test"
'


test_expect_success NO_CHAIN_LINT 'kvs: watch works (owner)'  '
        flux kvs unlink --namespace=$NAMESPACETMP-OWNER -Rf $DIR &&
        flux kvs put --namespace=$NAMESPACETMP-OWNER --json $DIR.watch=0 &&
        rm -f watch_out
        flux kvs watch --namespace=$NAMESPACETMP-OWNER -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "0" watch_out
        flux kvs put --namespace=$NAMESPACETMP-OWNER --json $DIR.watch=1 &&
        wait $watchpid
cat >expected <<-EOF &&
0
1
EOF
        test_cmp watch_out expected
'

test_expect_success 'kvs: watch fails (user)' '
        set_userid 9999 &&
        ! flux kvs watch --namespace=$NAMESPACETMP-OWNER -o -c 1 $DIR.test &&
        unset_userid
'

test_expect_success 'kvs: version fails (user)' '
        set_userid 9999 &&
        ! flux kvs version --namespace=$NAMESPACETMP-OWNER &&
        unset_userid
'

test_expect_success 'kvs: version fails on other ranks (user)' '
        ! flux exec -n -r 1 sh -c "FLUX_HANDLE_USERID=9999 \
                                FLUX_HANDLE_ROLEMASK=0x2 \
                                flux kvs version --namespace=$NAMESPACETMP-OWNER"
'

test_expect_success 'kvs: wait fails (user)' '
        set_userid 9999 &&
        ! flux kvs wait --namespace=$NAMESPACETMP-OWNER $DIR.test &&
        unset_userid
'

test_expect_success 'kvs: setroot pause / unpause works (owner)' '
      	${FLUX_BUILD_DIR}/t/kvs/setrootevents --pause --namespace=$NAMESPACETMP-OWNER &&
      	${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause --namespace=$NAMESPACETMP-OWNER
'

test_expect_success 'kvs: setroot pause / unpause fails (user)' '
        set_userid 9999 &&
      	! ${FLUX_BUILD_DIR}/t/kvs/setrootevents --pause &&
      	! ${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause &&
        unset_userid
'

test_expect_success 'kvs: namespace remove fails (user)' '
        set_userid 9999 &&
	! flux kvs namespace-remove $NAMESPACETMP-OWNER &&
        unset_userid
'

test_expect_success 'kvs: namespace remove works (owner)' '
        flux kvs put --namespace=$NAMESPACETMP-OWNER --json $DIR.tmp=1 &&
        test_kvs_key_namespace $NAMESPACETMP-OWNER $DIR.tmp 1 &&
        flux kvs namespace-remove $NAMESPACETMP-OWNER &&
        ! flux kvs get --namespace=$NAMESPACETMP-OWNER --json $DIR.tmp
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
        flux kvs put --namespace=$NAMESPACETMP-USER --json $DIR.test=1 &&
        test_kvs_key_namespace $NAMESPACETMP-USER $DIR.test 1 &&
        unset_userid
'

test_expect_success 'kvs: put/get works (owner)' '
        flux kvs put --namespace=$NAMESPACETMP-USER --json $DIR.test=2 &&
        test_kvs_key_namespace $NAMESPACETMP-USER $DIR.test 2
'

test_expect_success 'kvs: put fails (wrong user)' '
        set_userid 9000 &&
      	! flux kvs put --namespace=$NAMESPACETMP-USER --json $DIR.test=1 &&
        unset_userid
'

test_expect_success 'kvs: get works on other ranks (user)' '
        flux exec -n -r 1 sh -c "FLUX_HANDLE_USERID=9999 \
                              FLUX_HANDLE_ROLEMASK=0x2 \
                              flux kvs get --namespace=$NAMESPACETMP-USER $DIR.test"
'

test_expect_success 'kvs: get works on other ranks (owner)' '
        flux exec -n -r 1 sh -c "flux kvs get --namespace=$NAMESPACETMP-USER $DIR.test"
'

test_expect_success 'kvs: get fails (wrong user)' '
        set_userid 9000 &&
        ! flux kvs get --namespace=$NAMESPACETMP-USER --json $DIR.test &&
        unset_userid
'

test_expect_success 'kvs: get fails on other ranks (wrong user)' '
        ! flux exec -n -r 1 sh -c "FLUX_HANDLE_USERID=9000 \
                                FLUX_HANDLE_ROLEMASK=0x2 \
                                flux kvs get --namespace=$NAMESPACETMP-USER $DIR.test"
'

test_expect_success 'kvs: get works (wrong user, but with at reference)' '
        set_userid 9999 &&
        ref=`flux kvs get --namespace=$NAMESPACETMP-USER --treeobj .` &&
        unset_userid &&
        set_userid 9000 &&
        flux kvs get --namespace=$NAMESPACETMP-USER --at ${ref} --json $DIR.test &&
        unset_userid
'

test_expect_success NO_CHAIN_LINT 'kvs: watch works (user)'  '
        set_userid 9999 &&
        flux kvs unlink --namespace=$NAMESPACETMP-USER -Rf $DIR &&
        flux kvs put --namespace=$NAMESPACETMP-USER --json $DIR.watch=0 &&
        rm -f watch_out
        flux kvs watch --namespace=$NAMESPACETMP-USER -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "0" watch_out
        flux kvs put --namespace=$NAMESPACETMP-USER --json $DIR.watch=1 &&
        wait $watchpid
        unset_userid
cat >expected <<-EOF &&
0
1
EOF
        test_cmp watch_out expected
'

test_expect_success NO_CHAIN_LINT 'kvs: watch works (owner)'  '
        flux kvs unlink --namespace=$NAMESPACETMP-USER -Rf $DIR &&
        flux kvs put --namespace=$NAMESPACETMP-USER --json $DIR.watch=0 &&
        rm -f watch_out
        flux kvs watch --namespace=$NAMESPACETMP-USER -o -c 1 $DIR.watch >watch_out &
        watchpid=$! &&
        $waitfile -q -t 5 -p "0" watch_out
        flux kvs put --namespace=$NAMESPACETMP-USER --json $DIR.watch=1 &&
        wait $watchpid
cat >expected <<-EOF &&
0
1
EOF
        test_cmp watch_out expected
'

test_expect_success 'kvs: watch fails (wrong user)' '
        set_userid 9000 &&
        ! flux kvs watch --namespace=$NAMESPACETMP-USER -o -c 1 $DIR.test &&
        unset_userid
'

test_expect_success NO_CHAIN_LINT 'kvs: version & wait works (user)' '
        set_userid 9999 &&
        VERS=`flux kvs version --namespace=$NAMESPACETMP-USER` &&
        VERS=$((VERS + 1))
        flux kvs wait --namespace=$NAMESPACETMP-USER $VERS &
        kvswaitpid=$!
        flux kvs put --namespace=$NAMESPACETMP-USER --json $DIR.xxx=99
        unset_userid
        test_expect_code 0 wait $kvswaitpid
'

test_expect_success 'kvs: version works on other ranks (user)' '
        flux exec -n -r 1 sh -c "FLUX_HANDLE_USERID=9999 \
                              FLUX_HANDLE_ROLEMASK=0x2 \
                              flux kvs version --namespace=$NAMESPACETMP-USER"
'

test_expect_success 'kvs: version works on other ranks (owner)' '
        flux exec -n -r 1 sh -c "flux kvs version --namespace=$NAMESPACETMP-USER"
'

test_expect_success 'kvs: version fails (wrong user)' '
        set_userid 9000 &&
        ! flux kvs version --namespace=$NAMESPACETMP-USER &&
        unset_userid
'

test_expect_success 'kvs: version fails on other ranks (wrong user)' '
        ! flux exec -n -r 1 sh -c "FLUX_HANDLE_USERID=9000 \
                                FLUX_HANDLE_ROLEMASK=0x2 \
                                flux kvs version --namespace=$NAMESPACETMP-USER"
'

test_expect_success 'kvs: wait fails (wrong user)' '
        set_userid 9000 &&
        ! flux kvs wait --namespace=$NAMESPACETMP-USER $DIR.test &&
        unset_userid
'

test_expect_success 'kvs: setroot pause / unpause works (owner)' '
      	${FLUX_BUILD_DIR}/t/kvs/setrootevents --pause --namespace=$NAMESPACETMP-USER &&
      	${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause --namespace=$NAMESPACETMP-USER
'

test_expect_success 'kvs: setroot pause / unpause works (user)' '
        set_userid 9999 &&
      	${FLUX_BUILD_DIR}/t/kvs/setrootevents --pause --namespace=$NAMESPACETMP-USER &&
      	${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause --namespace=$NAMESPACETMP-USER &&
        unset_userid
'

test_expect_success 'kvs: setroot pause / unpause fails (wrong user)' '
        set_userid 9000 &&
      	! ${FLUX_BUILD_DIR}/t/kvs/setrootevents --pause --namespace=$NAMESPACETMP-USER &&
      	! ${FLUX_BUILD_DIR}/t/kvs/setrootevents --unpause --namespace=$NAMESPACETMP-USER &&
        unset_userid
'

test_expect_success 'kvs: namespace remove still fails (user)' '
        set_userid 9999 &&
        ! flux kvs namespace-remove $NAMESPACETMP-USER &&
        unset_userid
'

test_expect_success 'kvs: namespace remove still works (owner)' '
        flux kvs put --namespace=$NAMESPACETMP-USER --json $DIR.tmp=1 &&
        test_kvs_key_namespace $NAMESPACETMP-USER $DIR.tmp 1 &&
        flux kvs namespace-remove $NAMESPACETMP-USER &&
        ! flux kvs get --namespace=$NAMESPACETMP-USER --json $DIR.tmp
'

#
# namespace link cross
#

test_expect_success 'kvs: namespace create works (owner, for user)' '
	flux kvs namespace-create -o 9000 $NAMESPACETMP-SYMLINKNS1 &&
	flux kvs namespace-create -o 9001 $NAMESPACETMP-SYMLINKNS2 &&
	flux kvs namespace-create -o 9001 $NAMESPACETMP-SYMLINKNS3
'

test_expect_success 'kvs: symlink w/ Namespace works (owner)' '
        flux kvs put --namespace=${NAMESPACETMP}-SYMLINKNS1 $DIR.linktest=1 &&
        flux kvs put --namespace=${NAMESPACETMP}-SYMLINKNS2 $DIR.linktest=2 &&
        flux kvs link --namespace=${NAMESPACETMP}-SYMLINKNS1 --target-namespace=${NAMESPACETMP}-SYMLINKNS2 $DIR.linktest $DIR.link &&
        test_kvs_key_namespace ${NAMESPACETMP}-SYMLINKNS1 $DIR.link 2
'

test_expect_success 'kvs: symlinkw/ Namespace fails (wrong user)' '
        set_userid 9000 &&
        ! flux kvs get --namespace=${NAMESPACETMP}-SYMLINKNS1 $DIR.link &&
        unset_userid
'

test_expect_success 'kvs: symlink w/ Namespace works (user)' '
        set_userid 9001 &&
        flux kvs put --namespace=${NAMESPACETMP}-SYMLINKNS3 $DIR.linktest=3 &&
        flux kvs link --namespace=${NAMESPACETMP}-SYMLINKNS2 --target-namespace=${NAMESPACETMP}-SYMLINKNS3 $DIR.linktest $DIR.link &&
        test_kvs_key_namespace ${NAMESPACETMP}-SYMLINKNS2 $DIR.link 3 &&
        unset_userid
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
