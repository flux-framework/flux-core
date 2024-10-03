#!/bin/sh

test_description='Test content-files backing store service'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

test_under_flux 1 minimal -o,-Sstatedir=$(pwd)

BLOBREF=${FLUX_BUILD_DIR}/t/kvs/blobref
RPC=${FLUX_BUILD_DIR}/t/request/rpc
TEST_LOAD=${FLUX_BUILD_DIR}/src/modules/content-files/test_load
TEST_STORE=${FLUX_BUILD_DIR}/src/modules/content-files/test_store

SIZES="0 1 64 100 1000 1024 1025 8192 65536 262144 1048576 4194304"
LARGE_SIZES="8388608 10000000 16777216 33554432 67108864"

##
# Functions used by tests
##

# Usage: backing_load <hash
backing_load() {
        $RPC -r -R content-backing.load
}
# Usage: backing_store <blob >hash
backing_store() {
        $RPC -r -R content-backing.store
}
# Usage: make_blob size >blob
make_blob() {
	if test $1 -eq 0; then
		dd if=/dev/null 2>/dev/null
	else
		dd if=/dev/urandom count=1 bs=$1 2>/dev/null
	fi
}
# Usage: check_blob size
# Leaves behind blob.<size> and hash.<size>
check_blob() {
	make_blob $1 >blob.$1 &&
	backing_store <blob.$1 >hash.$1 &&
	backing_load <hash.$1 >blob.$1.check &&
	test_cmp blob.$1 blob.$1.check
}
# Usage: check_blob size
# Relies on existence of blob.<size> and hash.<size>
recheck_blob() {
	backing_load <hash.$1 >blob.$1.recheck &&
	test_cmp blob.$1 blob.$1.recheck
}
# Usage: recheck_cache_blob size
# Relies on existence of blob.<size>
recheck_cache_blob() {
	local blobref=$($BLOBREF sha1 <blob.$1)
	flux content load $blobref >blob.$1.cachecheck &&
	test_cmp blob.$1 blob.$1.cachecheck
}

test_expect_success 'load content module' '
	flux module load content
'

##
# Tests of the module by itself (no content cache)
##

test_expect_success 'content-files module load fails with unknown option' '
	test_must_fail flux module load content-files notoption
'

test_expect_success 'load content-files module' '
	flux module load content-files testing
'

test_expect_success 'load/store/verify key-values stored directly' '
	make_blob 140 >rawblob.140 &&
	$TEST_STORE $(pwd)/content.files testkey1 <rawblob.140 &&
	$TEST_LOAD $(pwd)/content.files testkey1 >rawblob.140.out &&
	test_cmp rawblob.140 rawblob.140.out
'

test_expect_success 'store/load/verify various size small blobs' '
	err=0 &&
	for size in $SIZES; do \
		if ! check_blob $size; then err=$(($err+1)); fi; \
	done &&
	test $err -eq 0
'

test_expect_success LONGTEST 'store/load/verify various size large blobs' '
	err=0 &&
	for size in $LARGE_SIZES; do \
		if ! check_blob $size; then err=$(($err+1)); fi; \
	done &&
	test $err -eq 0
'

test_expect_success 'reload content-files module' '
	flux module reload content-files testing
'

test_expect_success 'reload/verify various size small blobs' '
	err=0 &&
	for size in $SIZES; do \
		if ! recheck_blob $size; then err=$(($err+1)); fi; \
	done &&
	test $err -eq 0
'

test_expect_success LONGTEST 'reload/verify various size large blobs' '
	err=0 &&
	for size in $LARGE_SIZES; do \
		if ! recheck_blob $size; then err=$(($err+1)); fi; \
	done &&
	test $err -eq 0
'

test_expect_success 'load with invalid hash size fails with EPROTO' '
	test_must_fail backing_load </dev/null 2>badhash.err &&
	grep "Protocol error" badhash.err
'

##
# Tests of the module acting as backing store for content cache
##

test_expect_success 'reload content-files module without testing option' '
	flux module reload content-files
'

test_expect_success 'verify content.backing-module=content-files' '
        test "$(flux getattr content.backing-module)" = "content-files"
'

test_expect_success 'reload/verify various size small blobs through cache' '
	err=0 &&
	for size in $SIZES; do \
		if ! recheck_cache_blob $size; then err=$(($err+1)); fi; \
	done &&
	test $err -eq 0
'

test_expect_success LONGTEST 'reload/verify various size large blobs through cache' '
	err=0 &&
	for size in $LARGE_SIZES; do \
		if ! recheck_cache_blob $size; then err=$(($err+1)); fi; \
	done &&
	test $err -eq 0
'

test_expect_success 'flux module stats reports nonzero object count' '
	test $(flux module stats \
	    --type int --parse object_count content-files) -gt 0
'
test_expect_success 'reload content-files with truncate option' '
	flux module reload content-files truncate
'
test_expect_success 'flux module stats reports zero object count' '
	test $(flux module stats \
	    --type int --parse object_count content-files) -eq 0
'

test_expect_success 'checkpoint-put foo w/ rootref bar' '
	checkpoint_put foo bar
'

test_expect_success 'checkpoint-get foo returned rootref bar' '
        echo bar >rootref.exp &&
        checkpoint_get foo | jq -r .value | jq -r .rootref >rootref.out &&
        test_cmp rootref.exp rootref.out
'

# use grep instead of compare, incase of floating point rounding
test_expect_success 'checkpoint-get foo returned correct timestamp' '
        checkpoint_get foo | jq -r .value | jq -r .timestamp >timestamp.out &&
        grep 2.2 timestamp.out
'

test_expect_success 'checkpoint-put updates foo rootref to baz' '
        checkpoint_put foo baz
'

test_expect_success 'checkpoint-get foo returned rootref baz' '
        echo baz >rootref2.exp &&
        checkpoint_get foo | jq -r .value | jq -r .rootref >rootref2.out &&
        test_cmp rootref2.exp rootref2.out
'

test_expect_success 'reload content-files module' '
	flux module reload content-files
'

test_expect_success 'checkpoint-get foo still returns rootref baz' '
        echo baz >rootref3.exp &&
        checkpoint_get foo | jq -r .value | jq -r .rootref >rootref3.out &&
        test_cmp rootref3.exp rootref3.out
'

test_expect_success 'checkpoint-put updates foo rootref with longer rootref' '
        checkpoint_put foo abcdefghijklmnopqrstuvwxyz
'

test_expect_success 'checkpoint-get foo returned rootref with longer rootref' '
        echo abcdefghijklmnopqrstuvwxyz >rootref4.exp &&
        checkpoint_get foo | jq -r .value | jq -r .rootref >rootref4.out &&
        test_cmp rootref4.exp rootref4.out
'

test_expect_success 'checkpoint-put updates foo rootref to shorter rootref' '
        checkpoint_put foo foobar
'

test_expect_success 'checkpoint-get foo returned rootref with shorter rootref' '
        echo foobar >rootref5.exp &&
        checkpoint_get foo | jq -r .value | jq -r .rootref >rootref5.out &&
        test_cmp rootref5.exp rootref5.out
'

test_expect_success 'checkpoint-put updates foo rootref to boof' '
        checkpoint_put foo boof
'

test_expect_success 'checkpoint-backing-get foo returns rootref boof' '
        echo boof >rootref_backing.exp &&
        checkpoint_backing_get foo \
            | jq -r .value \
            | jq -r .rootref >rootref_backing.out &&
        test_cmp rootref_backing.exp rootref_backing.out
'

test_expect_success 'checkpoint-backing-put foo w/ rootref poof' '
        checkpoint_backing_put foo poof
'

test_expect_success 'checkpoint-get foo returned rootref boof' '
        echo poof >rootref6.exp &&
        checkpoint_get foo | jq -r .value | jq -r .rootref >rootref6.out &&
        test_cmp rootref6.exp rootref6.out
'

test_expect_success 'checkpoint-get bad request fails with EPROTO' '
	test_must_fail $RPC content.checkpoint-get </dev/null 2>badget.err &&
	grep "Protocol error" badget.err
'
test_expect_success 'checkpoint-put bad request fails with EPROTO' '
	test_must_fail $RPC content.checkpoint-put </dev/null 2>badput.err &&
	grep "Protocol error" badput.err
'

test_expect_success 'remove content-files module' '
	flux module remove content-files
'

test_expect_success 'checkpoint-put foo w/ rootref spoon' '
       checkpoint_put foo spoon
'

test_expect_success 'checkpoint-get foo returned rootref spoon' '
       echo spoon >rootref7.exp &&
       checkpoint_get foo | jq -r .value | jq -r .rootref >rootref7.out &&
       test_cmp rootref7.exp rootref7.out
'

test_expect_success 'load content-files module on rank 0' '
       flux module load content-files
'

# arg1 - expected reference
wait_checkpoint_flush() {
        local expected=$1
        local i=0
        while checkpoint_backing_get foo \
                | jq -r .value \
                | jq -r .rootref > checkpointflush.out \
              && [ $i -lt 50 ]
        do
            checkpoint=$(cat checkpointflush.out)
            if [ "${checkpoint}" = "${expected}" ]
            then
                return 0
            fi
            sleep 0.1
            i=$((i + 1))
        done
        return 1
}

test_expect_success 'checkpoint-backing-get foo returns spoon' '
       wait_checkpoint_flush spoon
'

test_expect_success 'flux module stats content-files is open to guests' '
	FLUX_HANDLE_ROLEMASK=0x2 \
	    flux module stats content-files >/dev/null
'

test_expect_success 'remove content-files module on rank 0' '
       flux content flush &&
       flux module remove content-files
'

test_expect_success 'remove content module' '
	flux module remove content
'

test_done
