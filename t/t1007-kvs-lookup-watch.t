#!/bin/sh

test_description='Test KVS get --watch'

. `dirname $0`/sharness.sh

test_under_flux 4 kvs

waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua
commit_order=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

test_expect_success 'flux kvs get --watch --count=1 works' '
	flux kvs put test.a=42 &&
	run_timeout 2 flux kvs get --watch --count=1 test.a >get.out &&
	echo 42 >get.exp &&
	test_cmp get.exp get.out
'

test_expect_success 'flux kvs get --watch works on alt namespace' '
	flux kvs namespace-create testns1 &&
	flux kvs --namespace=testns1 put test.b=foo &&
	run_timeout 2 flux kvs --namespace=testns1 get --watch --count=1 test.b >getns.out &&
	echo foo >getns.exp &&
	test_cmp getns.exp getns.out &&
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

test_expect_success NO_CHAIN_LINT 'flux kvs get --watch returns commit order' '
	flux kvs put test.c=1 &&
	flux kvs get --watch --count=20 test.c >seq.out &
	pid=$! &&
	$waitfile --count=1 --timeout=10 \
		  --pattern="[0-9]+" seq.out >/dev/null &&
	for i in $(seq 2 20); \
	    do flux kvs put --no-merge test.c=$i; \
	done &&
	$waitfile --count=20 --timeout=10 --pattern="[0-9]+" seq.out &&
	wait $pid &&
	test_monotonicity <seq.out
'

test_expect_success 'kvs/commit_order test works (similar to above, with higher concurrency)' '
	$FLUX_BUILD_DIR/t/kvs/commit_order -f 16 -c 1024 test.d
'

test_expect_success 'flux kvs get --watch fails on nonexistent key' '
	test_must_fail flux kvs get --watch --count=1 test.noexist
'

test_expect_success 'flux kvs get --watch fails on nonexistent namespace' '
	test_must_fail flux kvs --namespace=noexist \
		get --watch --count=1 test.noexist
'

test_expect_success NO_CHAIN_LINT 'flux kvs get --watch terminated by key removal' '
	flux kvs put test.e=1 &&
	flux kvs get --watch test.e >seq2.out &
	pid=$! &&
	$waitfile --count=1 --timeout=10 \
		  --pattern="[0-9]+" seq2.out >/dev/null &&
	flux kvs unlink test.e &&
	! wait $pid
'

test_expect_success NO_CHAIN_LINT 'flux kvs get --watch terminated by namespace removal' '
	flux kvs namespace-create testns2 &&
	flux kvs --namespace=testns2 put meep=1 &&
	flux kvs --namespace=testns2 get --watch meep >seq4.out &
	pid=$! &&
	$waitfile --count=1 --timeout=10 \
		  --pattern="[0-9]+" seq4.out >/dev/null &&
	flux kvs namespace-remove testns2 &&
	! wait $pid
'

test_expect_success NO_CHAIN_LINT 'flux kvs get --watch sees duplicate commited values' '
	flux kvs put test.f=1 &&

	flux kvs get --count=20 --watch test.f >seq3.out &
	pid=$! &&
	$waitfile --count=1 --timeout=10 \
		  --pattern="[0-9]+" seq3.out >/dev/null &&
	for i in $(seq 2 20); \
	    do flux kvs put --no-merge test.f=1; \
	done &&
	$waitfile --count=20 --timeout=10 --pattern="[0-9]+" seq3.out &&
	wait $pid
'

# Security checks

test_expect_success 'flux kvs get --watch denies guest access to primary namespace' '
	flux kvs put test.g=42 &&
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=9999 \
		flux kvs get --watch --count=1 test.g
'

test_expect_success 'flux kvs get --watch allows owner userid' '
	flux kvs put test.h=43 &&
	FLUX_HANDLE_ROLEMASK=0x2 \
		flux kvs get --watch --count=1 test.h
'

test_expect_success 'flux kvs get --watch allows guest access to its ns' '
	flux kvs namespace-create --owner=9999 testns3 &&
	flux kvs --namespace=testns3 put test.i=69 &&
	FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=9999 \
		flux kvs --namespace=testns3 get --watch --count=1 test.i &&
	flux kvs namespace-remove testns3
'

test_expect_success 'flux kvs get --watch denies guest access to anothers ns' '
	flux kvs namespace-create --owner=9999 testns4 &&
	flux kvs --namespace=testns4 put test.j=102 &&
	! FLUX_HANDLE_ROLEMASK=0x2 FLUX_HANDLE_USERID=9998 \
		flux kvs --namespace=testns4 get --watch --count=1 test.j &&
	flux kvs namespace-remove testns4
'

test_expect_success 'flux kvs get --watch allows owner access to guest ns' '
	flux kvs namespace-create --owner=9999 testns5 &&
	flux kvs --namespace=testns5 put test.k=100 &&
	! FLUX_HANDLE_ROLEMASK=0x1 FLUX_HANDLE_USERID=9998 \
		flux kvs --namespace=testns4 get --watch --count=1 test.k &&
	flux kvs namespace-remove testns4
'

test_done
