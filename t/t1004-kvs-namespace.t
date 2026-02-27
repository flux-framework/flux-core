#!/bin/sh

test_description='Test flux-kvs and kvs in flux session

These are tests for ensuring multiple namespaces work.
'

. `dirname $0`/util/wait-util.sh

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

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
NAMESPACEROOTREF=namespacerootref

namespace_create_loop() {
	wait_util "flux kvs namespace create $1"
}

get_kvs_namespace_all_ranks_loop() {
	wait_util "flux exec -n sh -c \"flux kvs get --namespace=$1 $2\""
}

get_kvs_namespace_fails_all_ranks_loop() {
	wait_util "flux exec -n sh -c \"! flux kvs get --namespace=$1 $2\""
}

wait_versionwaiters_nonzero() {
	wait_util "flux module stats --parse namespace.$1.#versionwaiters kvs > /dev/null 2>&1 \
		&& [ \"\$(flux module stats --parse namespace.$1.#versionwaiters kvs 2> /dev/null)\" != \"0\" ]"
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
	flux kvs put $DIR.test=1 &&
	test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 1
'

test_expect_success 'kvs: put with primary namespace works' '
	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.test=2 &&
	test_kvs_key $DIR.test 2
'

test_expect_success 'kvs: put/get with primary namespace works' '
	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.test=3 &&
	test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 3
'

test_expect_success 'kvs: unlink with primary namespace works' '
	flux kvs unlink --namespace=$PRIMARYNAMESPACE $DIR.test &&
	test_must_fail flux kvs get $DIR.test
'

test_expect_success 'kvs: dir with primary namespace works' '
	flux kvs put $DIR.a=1 &&
	flux kvs put $DIR.b=2 &&
	flux kvs put $DIR.c=3 &&
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
	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.xxx=99
	test_expect_code 0 wait $kvswaitpid
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
	flux kvs put --namespace=$NAMESPACETEST $DIR.test=1 &&
	test_kvs_key_namespace $NAMESPACETEST $DIR.test 1
'

test_expect_success 'kvs: unlink in new namespace works' '
	flux kvs unlink --namespace=$NAMESPACETEST $DIR.test &&
	! flux kvs get --namespace=$NAMESPACETEST $DIR.test
'

test_expect_success 'kvs: dir in new namespace works' '
	flux kvs put --namespace=$NAMESPACETEST $DIR.a=4 &&
	flux kvs put --namespace=$NAMESPACETEST $DIR.b=5 &&
	flux kvs put --namespace=$NAMESPACETEST $DIR.c=6 &&
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
	flux kvs put --namespace=$NAMESPACETEST $DIR.xxx=99
	test_expect_code 0 wait $kvswaitpid
'

test_expect_success 'kvs: namespace remove non existing namespace silently passes' '
	flux kvs namespace remove $NAMESPACETMP
'

test_expect_success 'kvs: namespace remove works' '
	flux kvs namespace create $NAMESPACETMP-BASIC &&
	flux kvs put --namespace=$NAMESPACETMP-BASIC $DIR.tmp=1 &&
	test_kvs_key_namespace $NAMESPACETMP-BASIC $DIR.tmp 1 &&
	flux kvs namespace remove $NAMESPACETMP-BASIC &&
	! flux kvs get --namespace=$NAMESPACETMP-BASIC $DIR.tmp
'

# A namespace create races against the namespace remove above, as we
# can't confirm if the namespace remove has garbage collected itself
# yet.  So we use namespace_create_loop() to iterate and try
# namespace create many times until it succeeds.
test_expect_success 'kvs: namespace can be re-created after remove' '
	namespace_create_loop $NAMESPACETMP-BASIC &&
	flux kvs put --namespace=$NAMESPACETMP-BASIC $DIR.recreate=1 &&
	test_kvs_key_namespace $NAMESPACETMP-BASIC $DIR.recreate 1 &&
	flux kvs namespace remove $NAMESPACETMP-BASIC &&
	! flux kvs get --namespace=$NAMESPACETMP-BASIC $DIR.recreate
'

test_expect_success 'kvs: removed namespace not listed' '
	! flux kvs namespace list | grep $NAMESPACETMP-BASIC
'

#
# Basic tests, data in new namespace available across ranks
#

test_expect_success 'kvs: put value in new namespace, available on other ranks' '
	flux kvs unlink --namespace=$NAMESPACETEST -Rf $DIR &&
	flux kvs put --namespace=$NAMESPACETEST $DIR.all=1 &&
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
	flux kvs put --namespace=$NAMESPACETMP-ALL $DIR.all=1 &&
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
	flux kvs put --namespace=$NAMESPACETMP-ALL $DIR.recreate=1 &&
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
# Namespace rootref initialization
#

test_expect_success 'kvs: namespace rootref setup' '
	flux kvs namespace create $NAMESPACEROOTREF-1 &&
	flux kvs put --namespace=$NAMESPACEROOTREF-1 $DIR.rootreftest=foobar &&
	test_kvs_key_namespace $NAMESPACEROOTREF-1 $DIR.rootreftest foobar &&
	flux kvs getroot --blobref --namespace=$NAMESPACEROOTREF-1 > rootref1
'

test_expect_success 'kvs: namespace create with init rootref' '
	flux kvs namespace create --rootref=$(cat rootref1) $NAMESPACEROOTREF-2 &&
	test_kvs_key_namespace $NAMESPACEROOTREF-1 $DIR.rootreftest foobar
'

test_expect_success 'kvs: namespaces dont clobber each other' '
	flux kvs put --namespace=$NAMESPACEROOTREF-1 $DIR.val=42 &&
	flux kvs put --namespace=$NAMESPACEROOTREF-2 $DIR.val=43 &&
	test_kvs_key_namespace $NAMESPACEROOTREF-1 $DIR.val 42 &&
	test_kvs_key_namespace $NAMESPACEROOTREF-2 $DIR.val 43
'

BADROOTREF="sha1-0123456789abcdef0123456789abcdef01234567"
test_expect_success 'kvs: namespace create can take bad blobref' '
	flux kvs namespace create --rootref=$BADROOTREF $NAMESPACEROOTREF-3 &&
	flux kvs get --namespace=$NAMESPACEROOTREF-3 --treeobj .
'

test_expect_success 'kvs: namespace with bad rootref fails otherwise' '
	test_must_fail flux kvs ls --namespace=$NAMESPACEROOTREF-3 .
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
	! flux kvs get --namespace=$NAMESPACEBAD $DIR.test
'

test_expect_success 'kvs: get fails on invalid namespace on rank 1' '
	! flux exec -n -r 1 sh -c "flux kvs get --namespace=$NAMESPACEBAD $DIR.test"
'

test_expect_success 'kvs: put fails on invalid namespace' '
	! flux kvs put --namespace=$NAMESPACEBAD $DIR.test=1
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

test_expect_success NO_CHAIN_LINT 'kvs: wait recognizes removed namespace' '
	flux kvs namespace create $NAMESPACETMP-REMOVE-WAIT &&
	VERS=$(flux kvs version --namespace=$NAMESPACETMP-REMOVE-WAIT) &&
	VERS=$((VERS + 1))
	flux kvs wait --namespace=$NAMESPACETMP-REMOVE-WAIT $VERS > wait_out 2>&1 &
	waitpid=$! &&
	wait_versionwaiters_nonzero $NAMESPACETMP-REMOVE-WAIT &&
	flux kvs namespace remove $NAMESPACETMP-REMOVE-WAIT &&
	! wait $waitpid &&
	grep "flux_kvs_wait_version: $(strerror_symbol ENOTSUP)" wait_out
'

#
# Basic tests - no pollution between namespaces
#

test_expect_success 'kvs: put/get in different namespaces works' '
	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.test=1 &&
	flux kvs put --namespace=$NAMESPACETEST $DIR.test=2 &&
	test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.test 1 &&
	test_kvs_key_namespace $NAMESPACETEST $DIR.test 2
'

test_expect_success 'kvs: unlink in different namespaces works' '
	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.testA=1 &&
	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.testB=1 &&
	flux kvs put --namespace=$NAMESPACETEST $DIR.testA=2 &&
	flux kvs put --namespace=$NAMESPACETEST $DIR.testB=2 &&
	flux kvs unlink --namespace=$PRIMARYNAMESPACE $DIR.testA &&
	flux kvs unlink --namespace=$NAMESPACETEST $DIR.testB &&
	test_kvs_key_namespace $PRIMARYNAMESPACE $DIR.testB 1 &&
	test_kvs_key_namespace $NAMESPACETEST $DIR.testA 2 &&
	! flux kvs get --namespace=$PRIMARYNAMESPACE $DIR.testA &&
	! flux kvs get --namespace=$NAMESPACETEST $DIR.testB
'

test_expect_success 'kvs: dir in different namespace works' '
	flux kvs unlink --namespace=$PRIMARYNAMESPACE -Rf $DIR &&
	flux kvs unlink --namespace=$NAMESPACETEST -Rf $DIR &&
	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.a=10 &&
	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.b=11 &&
	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.c=12 &&
	flux kvs put --namespace=$NAMESPACETEST $DIR.a=13 &&
	flux kvs put --namespace=$NAMESPACETEST $DIR.b=14 &&
	flux kvs put --namespace=$NAMESPACETEST $DIR.c=15 &&
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
	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.subdirA.A=A &&
	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.subdirA.B=B &&
	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.subdirB.A=A &&
	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.subdirB.A=B &&
	flux kvs put --namespace=$NAMESPACETEST $DIR.subdirA.A=A &&
	flux kvs put --namespace=$NAMESPACETEST $DIR.subdirA.B=B &&
	flux kvs put --namespace=$NAMESPACETEST $DIR.subdirB.A=A &&
	flux kvs put --namespace=$NAMESPACETEST $DIR.subdirB.A=B &&
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

	flux kvs put --namespace=$PRIMARYNAMESPACE $DIR.xxx=X
	flux kvs put --namespace=$NAMESPACETEST $DIR.xxx=X

	test_expect_code 0 wait $primarykvswaitpid
	test_expect_code 0 wait $testkvswaitpid
'

#
# ensure no lingering pending requests
#

test_expect_success 'kvs: no pending requests at end of tests' '
	pendingcount=$(flux module stats -p pending_requests kvs) &&
	test $pendingcount -eq 0
'

test_done
