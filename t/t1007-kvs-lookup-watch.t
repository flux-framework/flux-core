#!/bin/sh

test_description='Test KVS get --watch'

. `dirname $0`/kvs/kvs-helper.sh

. `dirname $0`/sharness.sh

test_under_flux 4 kvs

waitfile=${SHARNESS_TEST_SRCDIR}/scripts/waitfile.lua

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

test_expect_success NO_CHAIN_LINT 'flux kvs get --watch and --uniq do not see duplicate commited values' '
	flux kvs put test.f=1 &&

	flux kvs get --count=3 --watch --uniq test.f >seq4.out &
	pid=$! &&
	$waitfile --count=1 --timeout=10 \
		  --pattern="[0-9]+" seq4.out >/dev/null &&
	for i in $(seq 2 20); \
	    do flux kvs put --no-merge test.f=1; \
	done &&
	for i in $(seq 2 20); \
	    do flux kvs put --no-merge test.f=2; \
	done &&
	for i in $(seq 2 20); \
	    do flux kvs put --no-merge test.f=3; \
	done &&
	cat >expected <<-EOF &&
1
2
3
	EOF
	wait $pid &&
	test_cmp expected seq4.out
'

# in --watch & --waitcreate tests, call wait_watcherscount_nonzero to
# ensure background watcher has started, otherwise test can be racy

test_expect_success NO_CHAIN_LINT 'flux kvs get, basic --watch & --waitcreate works' '
        ! flux kvs get --watch test.create_later &&
        flux kvs get --watch --waitcreate --count=1 \
                     test.create_later > waitcreate.out &
        pid=$! &&
        wait_watcherscount_nonzero primary &&
        flux kvs put test.create_later=0 &&
	$waitfile --count=1 --timeout=10 \
		  --pattern="0" waitcreate.out >/dev/null &&
        wait $pid
'

test_expect_success NO_CHAIN_LINT 'flux kvs get, --watch & --waitcreate & remove key works' '
        ! flux kvs get --watch test.create_and_remove &&
        flux kvs get --watch --waitcreate --count=2 \
                     test.create_and_remove > waitcreate2.out 2>&1 &
        pid=$! &&
        wait_watcherscount_nonzero primary &&
        flux kvs put test.create_and_remove=0 &&
	$waitfile --count=1 --timeout=10 \
		  --pattern="0" waitcreate2.out >/dev/null &&
        flux kvs unlink test.create_and_remove &&
        ! wait $pid &&
        grep "No such file or directory" waitcreate2.out
'

test_expect_success NO_CHAIN_LINT 'flux kvs get, --watch & --waitcreate, create & remove namespace works' '
        ! flux kvs --namespace=ns_create_and_remove get --watch test.ns_create_and_remove &&
        flux kvs --namespace=ns_create_and_remove get --watch --waitcreate --count=2 \
                     test.ns_create_and_remove > waitcreate4.out 2>&1 &
        pid=$! &&
        wait_watcherscount_nonzero ns_create_and_remove &&
        flux kvs namespace-create ns_create_and_remove &&
        flux kvs --namespace=ns_create_and_remove put test.ns_create_and_remove=0 &&
        $waitfile --count=1 --timeout=10 \
                  --pattern="0" waitcreate4.out >/dev/null &&
        flux kvs namespace-remove ns_create_and_remove &&
        ! wait $pid &&
        grep "Operation not supported" waitcreate4.out
'

# full checks

# in full checks, we create a directory that we will use to
# get a treeobj.  We then use that treeobj to overwrite another
# directory.

# to handle racy issues, wait until a value has been seen by a get
# --watch.  Note that we can't use waitfile or flux kvs get here, b/c
# we are specifically testing against --watch.
wait_kvs_value() {
        key=$1
        value=$2
        i=0
        while [ "$(flux kvs get --watch --count=1 $key 2> /dev/null)" != "$value" ] \
              && [ $i -lt ${KVS_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

test_expect_success NO_CHAIN_LINT 'flux kvs get --watch w/o --full doesnt detect change' '
        flux kvs unlink -Rf test &&
        flux kvs put test.dir_orig.a="abc" &&
        flux kvs get --watch --count=2 test.dir_orig.a > full1.out 2>&1 &
        pid=$! &&
        wait_watcherscount_nonzero primary &&
        flux kvs put test.dir_new.a="xyz" &&
        DIRREF=$(flux kvs get --treeobj test.dir_new) &&
        flux kvs put --treeobj test.dir_orig="${DIRREF}" &&
        wait_kvs_value test.dir_orig.a xyz &&
        flux kvs put test.dir_orig.a="def" &&
        $waitfile --count=1 --timeout=10 \
                  --pattern="def" full1.out >/dev/null &&
        wait $pid
'

# to handle racy issues, wait until ENOENT has been seen by a get
# --watch.  Note that we can't use waitfile or flux kvs get here, b/c
# we are specifically testing against --watch
wait_kvs_enoent() {
        key=$1
        i=0
        while flux kvs get --watch --count=1 $key 2> /dev/null \
              && [ $i -lt ${KVS_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

test_expect_success NO_CHAIN_LINT 'flux kvs get --watch w/o --full doesnt detect ENOENT' '
        flux kvs unlink -Rf test &&
        flux kvs put test.dir_orig.a="abc" &&
        flux kvs get --watch --count=2 test.dir_orig.a > full2.out 2>&1 &
        pid=$! &&
        wait_watcherscount_nonzero primary &&
        flux kvs put test.dir_new.b="xyz" &&
        DIRREF=$(flux kvs get --treeobj test.dir_new) &&
        flux kvs put --treeobj test.dir_orig="${DIRREF}" &&
        wait_kvs_enoent test.dir_orig.a &&
        flux kvs put test.dir_orig.a="def" &&
        $waitfile --count=1 --timeout=10 \
                  --pattern="def" full2.out >/dev/null &&
        wait $pid
'

test_expect_success NO_CHAIN_LINT 'flux kvs get --watch w/ --full detects change' '
        flux kvs unlink -Rf test &&
        flux kvs put test.dir_orig.a="abc" &&
        flux kvs get --watch --full --count=2 test.dir_orig.a > full3.out 2>&1 &
        pid=$! &&
        wait_watcherscount_nonzero primary &&
        flux kvs put test.dir_new.a="xyz" &&
        DIRREF=$(flux kvs get --treeobj test.dir_new) &&
        flux kvs put --treeobj test.dir_orig="${DIRREF}" &&
        $waitfile --count=1 --timeout=10 \
                  --pattern="xyz" full3.out >/dev/null &&
        wait $pid
'

test_expect_success NO_CHAIN_LINT 'flux kvs get --watch w/ --full detects ENOENT' '
        flux kvs unlink -Rf test &&
        flux kvs put test.dir_orig.a="abc" &&
        flux kvs get --watch --full --count=2 test.dir_orig.a > full4.out 2>&1 &
        pid=$! &&
        wait_watcherscount_nonzero primary &&
        flux kvs put test.dir_new.b="xyz" &&
        DIRREF=$(flux kvs get --treeobj test.dir_new) &&
        flux kvs put --treeobj test.dir_orig="${DIRREF}" &&
        ! wait $pid
'

test_expect_success NO_CHAIN_LINT 'flux kvs get --watch w/ --full works with changing data sizes' '
        flux kvs unlink -Rf test &&
        flux kvs put test.dir.a="abc" &&
        flux kvs get --watch --full --count=5 test.dir.a > full5.out 2>&1 &
        pid=$! &&
        wait_watcherscount_nonzero primary &&
        flux kvs put test.dir.a="abcdefghijklmnopqrstuvwxyz" &&
        flux kvs put test.dir.a="xyz" &&
        flux kvs put test.dir.a="abcdefghijklmnopqrstuvwxyz" &&
        flux kvs put test.dir.a="abc" &&
        wait $pid &&
	cat >expected <<-EOF &&
abc
abcdefghijklmnopqrstuvwxyz
xyz
abcdefghijklmnopqrstuvwxyz
abc
	EOF
	test_cmp expected full5.out
'

test_expect_success NO_CHAIN_LINT 'flux kvs get --watch w/ --full doesnt work with non-changing data' '
        flux kvs unlink -Rf test &&
        flux kvs put test.dir.a="abc" &&
        flux kvs get --watch --full --count=3 test.dir.a > full6.out 2>&1 &
        pid=$! &&
        wait_watcherscount_nonzero primary &&
        flux kvs put test.dir.a="abc" &&
        flux kvs put test.dir.a="abcdefghijklmnopqrstuvwxyz" &&
        flux kvs put test.dir.a="abcdefghijklmnopqrstuvwxyz" &&
        flux kvs put test.dir.a="xyz" &&
        wait $pid &&
	cat >expected <<-EOF &&
abc
abcdefghijklmnopqrstuvwxyz
xyz
	EOF
	test_cmp expected full6.out
'

test_expect_success NO_CHAIN_LINT 'flux kvs get --watch w/ --full & --waitcreate works' '
        flux kvs unlink -Rf test &&
        flux kvs get --watch --full --waitcreate --count=3 test.dir.a > full7.out 2>&1 &
        pid=$! &&
        wait_watcherscount_nonzero primary &&
        flux kvs put test.dir.a="abc" &&
        flux kvs put test.dir.a="def" &&
        flux kvs put test.dir.a="xyz" &&
        wait $pid &&
	cat >expected <<-EOF &&
abc
def
xyz
	EOF
	test_cmp expected full7.out
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
