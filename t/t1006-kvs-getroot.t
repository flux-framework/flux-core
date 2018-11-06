#!/bin/sh

test_description='Test KVS getroot [--watch]'

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

test_under_flux 4 kvs

waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

test_expect_success 'flux kvs getroot returns valid dirref object' '
	flux kvs put test.a=42 &&
	DIRREF=$(flux kvs getroot) &&
	flux kvs put test.a=43 &&
	flux kvs get --at "$DIRREF" test.a >get.out &&
	echo 42 >get.exp &&
	test_cmp get.exp get.out
'

test_expect_success 'flux kvs getroot --blobref returns valid blobref' '
	BLOBREF=$(flux kvs getroot --blobref) &&
	flux content load $BLOBREF >/dev/null
'

test_expect_success 'flux kvs getroot --watch --count=1 --blobref also works' '
	BLOBREF=$(flux kvs getroot --watch --count=1 --blobref) &&
	flux content load $BLOBREF >/dev/null
'

test_expect_success 'flux kvs getroot --watch fails on nonexistent namespace' '
	! flux kvs --namespace=noexist getroot --watch --count=1
'

# in --watch & --waitcreate tests, call wait_watcherscount_nonzero to
# ensure background watcher has started, otherwise test can be racy

test_expect_success 'flux kvs getroot --watch and --waitcreate works' '
	! flux kvs --namespace=ns_create_later getroot --watch --count=1 &&
        flux kvs --namespace=ns_create_later getroot --watch --waitcreate \
                 --count=1 > waitcreate.out &
        pid=$! &&
        echo "1" &&
        wait_watcherscount_nonzero ns_create_later &&
        echo "2" &&
        flux kvs namespace-create ns_create_later &&
        echo "3" &&
        wait $pid &&
        echo "4" &&
        cat waitcreate.out &&
        flux kvs --namespace=ns_create_later getroot > waitcreate.exp &&
        echo "5" &&
        cat waitcreate.exp &&
        test_cmp waitcreate.exp waitcreate.out
'

test_expect_success 'flux kvs getroot --watch and --waitcreate and remove namespace works' '
	! flux kvs --namespace=ns_create_and_remove getroot --watch --count=1 &&
        flux kvs --namespace=ns_create_and_remove getroot --watch --waitcreate \
                 --count=2 > waitcreate2.out 2>&1 &
        pid=$! &&
        echo "1" &&
        wait_watcherscount_nonzero ns_create_and_remove &&
        echo "2" &&
        flux kvs namespace-create ns_create_and_remove &&
        echo "3" &&
        flux kvs --namespace=ns_create_and_remove getroot --blobref > waitcreate2.treeobj &&
        echo "4" &&
        cat waitcreate2.treeobj &&
        flux kvs namespace-remove ns_create_and_remove &&
        echo "5" &&
        ! wait $pid &&
        echo "6" &&
        cat waitcreate2.out &&
        treeobj=`cat waitcreate2.treeobj` &&
        echo "7" &&
        echo $treeobj &&
        grep "$treeobj" waitcreate2.out &&
        echo "8" &&
        grep "Operation not supported" waitcreate2.out
'

test_expect_success 'flux kvs getroot --sequence returns increasing rootseq' '
	SEQ=$(flux kvs getroot --sequence) &&
	flux kvs put test.b=hello &&
	SEQ2=$(flux kvs getroot --sequence) &&
	test $SEQ -lt $SEQ2
'

test_expect_success 'flux kvs getroot --owner returns instance owner' '
	OWNER=$(flux kvs getroot --owner) &&
	test $OWNER -eq $(id -u)
'

test_expect_success 'flux kvs getroot works on alt namespace' '
	flux kvs namespace-create testns1 &&
	SEQ=$(flux kvs --namespace=testns1 getroot --sequence) &&
	test $SEQ -eq 0 &&
	flux kvs --namespace=testns1 put test.c=moop &&
	SEQ2=$(flux kvs --namespace=testns1 getroot --sequence) &&
	test $SEQ -lt $SEQ2 &&
	flux kvs namespace-remove testns1
'

# Check that stdin contains an integer on each line that
# is one more than the integer on the previous line.
test_monotonicity() {
	local item
	local prev=0
	while read item; do
		test $prev -gt 0 && test $item -ne $(($prev+1)) && return 1
		prev=$item
	done
	return 0
}

test_expect_success NO_CHAIN_LINT 'flux kvs getroot --watch yields monotonic sequence' '
	flux kvs getroot --watch --count=20 --sequence >seq.out &
	pid=$! &&
	$waitfile --count=1 --timeout=10 \
	          --pattern="[0-9]+" seq.out >/dev/null &&
	for i in $(seq 1 20); \
	    do flux kvs put --no-merge test.c=$i; \
	done &&
	$waitfile --count=20 --timeout=10 --pattern="[0-9]+" seq.out &&
	wait $pid &&
	test_monotonicity <seq.out
'

test_expect_success 'kvs-watch stats reports no watchers when there are no watchers' '
	count=$(flux module stats --parse=watchers kvs-watch) &&
	test $count -eq 0
'

test_expect_success 'kvs-watch stats reports no namespaces when there are no watchers' '
	count=$(flux module stats --parse=namespace-count kvs-watch) &&
	test $count -eq 0
'

test_expect_success NO_CHAIN_LINT 'kvs-watch stats reports active watcher' '
	flux kvs getroot --watch --count=2 --sequence >seq2.out &
	pid=$! &&
	$waitfile --count=1 --timeout=10 --pattern="[0-9]+" seq2.out &&
	count=$(flux module stats --parse=watchers kvs-watch) &&
	test $count -eq 1 &&
	count=$(flux module stats --parse=namespace-count kvs-watch) &&
	test $count -eq 1 &&
	flux kvs put --no-merge test.d=foo &&
	wait $pid
'

test_expect_success NO_CHAIN_LINT 'kvs-watch namespace removal terminates stream' '
	flux kvs namespace-create meep &&
	flux kvs --namespace=meep getroot --watch >seq3.out &
	pid=$! &&
	$waitfile --timeout=10 seq3.out &&
	flux kvs namespace-remove meep &&
	! wait $pid
'

# Security checks

test_expect_success 'flux kvs getroot [--watch] denies guest access to primary ns' '
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=9999 \
		flux kvs getroot &&
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=9999 \
		flux kvs getroot --watch --count=1
'

test_expect_success 'flux kvs getroot [--watch] allows owner userid' '
	FLUX_HANDLE_ROLEMASK=0x2 \
		flux kvs getroot &&
	FLUX_HANDLE_ROLEMASK=0x2 \
		flux kvs getroot --watch --count=1
'

test_expect_success 'flux kvs getroot [--watch] allows FLUX_ROLE_OWNER' '
	FLUX_HANDLE_ROLEMASK=0x1 FLUX_HANDLE_USERID=9999 \
		flux kvs getroot &&
	FLUX_HANDLE_ROLEMASK=0x1 FLUX_HANDLE_USERID=9999 \
		flux kvs getroot --watch --count=1
'

test_expect_success 'flux kvs getroot [--watch] allows guest access to its ns' '
	flux kvs namespace-create --owner=9999 testns2 &&
	FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=9999 \
		flux kvs --namespace=testns2 getroot &&
	FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=9999 \
		flux kvs --namespace=testns2 getroot --watch --count=1 &&
	flux kvs namespace-remove testns2
'

test_expect_success 'flux kvs getroot [--watch] denies guest access to anothers ns' '
	flux kvs namespace-create --owner=9999 testns3 &&
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=9998 \
		flux kvs --namespace=testns3 getroot &&
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=9998 \
		flux kvs --namespace=testns3 getroot --watch --count=1 &&
	flux kvs namespace-remove testns3
'

test_expect_success 'flux kvs getroot [--watch] allows owner access to guest ns' '
	flux kvs namespace-create --owner=9999 testns4 &&
	FLUX_HANDLE_ROLEMASK=0x1 FLUX_HANDLE_USERID=9998 \
		flux kvs --namespace=testns4 getroot &&
	FLUX_HANDLE_ROLEMASK=0x1 FLUX_HANDLE_USERID=9998 \
		flux kvs --namespace=testns4 getroot --watch --count=1 &&
	flux kvs namespace-remove testns4
'

test_done
