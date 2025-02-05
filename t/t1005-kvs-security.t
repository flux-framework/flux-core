#!/bin/sh

test_description='Test flux-kvs and kvs in flux session

These are tests for ensuring multiple namespaces work.
'

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

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
	! flux kvs namespace create $NAMESPACETMP-OWNER &&
        unset_userid
'

test_expect_success 'kvs: namespace create works (owner)' '
	flux kvs namespace create $NAMESPACETMP-OWNER
'

test_expect_success 'kvs: put fails (user)' '
        set_userid 9999 &&
      	! flux kvs put --namespace=$NAMESPACETMP-OWNER $DIR.test=1 &&
        unset_userid
'

test_expect_success 'kvs: put works (owner)' '
        flux kvs put --namespace=$NAMESPACETMP-OWNER $DIR.test=1 &&
        test_kvs_key_namespace $NAMESPACETMP-OWNER $DIR.test 1
'

test_expect_success 'kvs: get fails (user)' '
        set_userid 9999 &&
	! flux kvs get --namespace=$NAMESPACETMP-OWNER $DIR.test &&
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
	! flux kvs namespace remove $NAMESPACETMP-OWNER &&
        unset_userid
'

test_expect_success 'kvs: namespace remove works (owner)' '
        flux kvs put --namespace=$NAMESPACETMP-OWNER $DIR.tmp=1 &&
        test_kvs_key_namespace $NAMESPACETMP-OWNER $DIR.tmp 1 &&
        flux kvs namespace remove $NAMESPACETMP-OWNER &&
        ! flux kvs get --namespace=$NAMESPACETMP-OWNER $DIR.tmp
'

#
# Namespace tests owned by user
#

test_expect_success 'kvs: namespace create works (owner, for user)' '
	flux kvs namespace create -o 9999 $NAMESPACETMP-USER
'

test_expect_success 'kvs: namespace listed with correct owner' '
        flux kvs namespace list | grep $NAMESPACETMP-USER | grep 9999
'

test_expect_success 'kvs: namespace put/get works (user)' '
        set_userid 9999 &&
        flux kvs put --namespace=$NAMESPACETMP-USER $DIR.test=1 &&
        test_kvs_key_namespace $NAMESPACETMP-USER $DIR.test 1 &&
        unset_userid
'

test_expect_success 'kvs: put/get works (owner)' '
        flux kvs put --namespace=$NAMESPACETMP-USER $DIR.test=2 &&
        test_kvs_key_namespace $NAMESPACETMP-USER $DIR.test 2
'

test_expect_success 'kvs: put fails (wrong user)' '
        set_userid 9000 &&
      	! flux kvs put --namespace=$NAMESPACETMP-USER $DIR.test=1 &&
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
        ! flux kvs get --namespace=$NAMESPACETMP-USER $DIR.test &&
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
        flux kvs get --namespace=$NAMESPACETMP-USER --at ${ref} $DIR.test &&
        unset_userid
'

test_expect_success NO_CHAIN_LINT 'kvs: version & wait works (user)' '
        set_userid 9999 &&
        VERS=`flux kvs version --namespace=$NAMESPACETMP-USER` &&
        VERS=$((VERS + 1))
        flux kvs wait --namespace=$NAMESPACETMP-USER $VERS &
        kvswaitpid=$!
        flux kvs put --namespace=$NAMESPACETMP-USER $DIR.xxx=99
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
        ! flux kvs namespace remove $NAMESPACETMP-USER &&
        unset_userid
'

test_expect_success 'kvs: namespace remove still works (owner)' '
        flux kvs put --namespace=$NAMESPACETMP-USER $DIR.tmp=1 &&
        test_kvs_key_namespace $NAMESPACETMP-USER $DIR.tmp 1 &&
        flux kvs namespace remove $NAMESPACETMP-USER &&
        ! flux kvs get --namespace=$NAMESPACETMP-USER $DIR.tmp
'

#
# namespace link cross
#

test_expect_success 'kvs: namespace create works (owner, for user)' '
	flux kvs namespace create -o 9000 $NAMESPACETMP-SYMLINKNS1 &&
	flux kvs namespace create -o 9001 $NAMESPACETMP-SYMLINKNS2 &&
	flux kvs namespace create -o 9001 $NAMESPACETMP-SYMLINKNS3
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

#
# Basic tests, guest commits are limited
#

test_expect_success 'kvs: create test ns for user 9999' '
	flux kvs namespace create -o 9999 $NAMESPACETMP-SYMLINK
'
test_expect_success 'kvs: owner can make a symlink in the test ns' '
	flux kvs link --namespace=$NAMESPACETMP-SYMLINK a b
'
test_expect_success 'kvs: guest can put a val in the test ns' '
	set_userid 9999 &&
	flux kvs put --namespace=$NAMESPACETMP-SYMLINK aa=42 &&
	unset_userid
'
test_expect_success 'kvs: guest can unlink a val in the test ns' '
	set_userid 9999 &&
	flux kvs unlink --namespace=$NAMESPACETMP-SYMLINK aa &&
	unset_userid
'
test_expect_success 'kvs: guest can make an empty dir in the test ns' '
	set_userid 9999 &&
	flux kvs mkdir --namespace=$NAMESPACETMP-SYMLINK bb &&
	unset_userid
'
test_expect_success 'kvs: guest can unlink a dir in the test ns' '
	set_userid 9999 &&
	flux kvs unlink --namespace=$NAMESPACETMP-SYMLINK bb &&
	unset_userid
'
test_expect_success 'kvs: guest cannot make a symlink in the test ns' '
	set_userid 9999 &&
	test_must_fail flux kvs link \
	    --namespace=$NAMESPACETMP-SYMLINK c d 2>link.err &&
	grep "Operation not permitted" link.err &&
	unset_userid
'
test_expect_success 'kvs: guest can put a val treeobj' "
	set_userid 9999 &&
	flux kvs put --treeobj \
	    --namespace=$NAMESPACETMP-SYMLINK \
	    cc='{\"data\":\"Yg==\",\"type\":\"val\",\"ver\":1}' &&
	unset_userid
"
test_expect_success 'kvs: guest cannot put a symlink treeobj' "
	set_userid 9999 &&
	test_must_fail flux kvs put --treeobj \
	    --namespace=$NAMESPACETMP-SYMLINK \
	    y='{\"data\":{\"target\":\"x\"},\"type\":\"symlink\",\"ver\":1}' \
	    2>treeobj_symlink.err &&
	grep 'Operation not permitted' treeobj_symlink.err &&
	unset_userid
"
test_expect_success 'kvs: guest cannot put a valref treeobj' "
	set_userid 9999 &&
	test_must_fail flux kvs put --treeobj \
	    --namespace=$NAMESPACETMP-SYMLINK \
	    z='{\"data\":[\"sha1-8727ddf86fd56772f4ed38703d24d93e9d9e7fa6\"],\"type\":\"valref\",\"ver\":1}' \
	    2>treeobj_valref.err &&
	grep 'Operation not permitted' treeobj_valref.err &&
	unset_userid
"
test_expect_success 'kvs: guest cannot put a dirref treeobj' "
	set_userid 9999 &&
	test_must_fail flux kvs put --treeobj \
	    --namespace=$NAMESPACETMP-SYMLINK \
	    z='{\"data\":[\"sha1-8727ddf86fd56772f4ed38703d24d93e9d9e7fa6\"],\"type\":\"dirref\",\"ver\":1}' \
	    2>treeobj_dirref.err &&
	grep 'Operation not permitted' treeobj_dirref.err &&
	unset_userid
"
test_expect_success 'kvs: guest cannot put a non-empty dir treeobj' "
	set_userid 9999 &&
	test_must_fail flux kvs put --treeobj \
	    --namespace=$NAMESPACETMP-SYMLINK \
	    q='{\"data\":{\"b\":{\"data\":\"NDI=\",\"type\":\"val\",\"ver\":1}},\"type\":\"dir\",\"ver\":1}' \
	    2>treeobj_nonemptydir.err &&
	grep 'Operation not permitted' treeobj_nonemptydir.err &&
	unset_userid
"
test_expect_success 'kvs: guest cannot put a dir treeobj containing symlink' "
	set_userid 9999 &&
	test_must_fail flux kvs put --treeobj \
	    --namespace=$NAMESPACETMP-SYMLINK \
	    qq='{\"data\":{\"b\":{\"data\":{\"target\":\"x\"},\"type\":\"symlink\",\"ver\":1}},\"type\":\"dir\",\"ver\":1}' \
	    2>treeobj_symdir.err &&
	    grep 'Operation not permitted' treeobj_symdir.err &&
	unset_userid
"
test_expect_success 'kvs: owner can make a symlink in the test ns on rank 1' '
	flux exec -n -r 1 \
	    sh -c "flux kvs link --namespace=$NAMESPACETMP-SYMLINK e f"
'
test_expect_success 'kvs: guest cannot make a symlink in the test ns on rank 1' '
	test_must_fail flux exec -n -r 1 \
	    sh -c "FLUX_HANDLE_USERID=9999 FLUX_HANDLE_ROLEMASK=0x2 \
		flux kvs link --namespace=$NAMESPACETMP-SYMLINK g h" \
	    2>link2.err &&
	grep "Operation not permitted" link2.err
'

#
# Basic tests, user can't perform non-namespace operations
#

test_expect_success 'kvs: dropcache fails (user)' '
        set_userid 9999 &&
        ! flux kvs dropcache &&
        unset_userid
'

test_expect_success 'kvs: stats works (user)' '
        set_userid 9999 &&
        flux module stats kvs >/dev/null &&
        unset_userid
'

test_expect_success 'kvs-watch: stats works (user)' '
        set_userid 9999 &&
        flux module stats kvs-watch >/dev/null &&
        unset_userid
'

#
# ensure no lingering pending requests
#

test_expect_success 'kvs: no pending requests at end of tests' '
	pendingcount=$(flux module stats -p pending_requests kvs) &&
	test $pendingcount -eq 0
'

test_done
