#!/bin/sh

test_description='Test content-s3 backing store service'

. `dirname $0`/sharness.sh

if test -z "$S3_HOSTNAME"; then
	skip_all='skipping content-s3 test since S3_HOSTNAME not set'
	test_done
fi

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

export FLUX_CONF_DIR=$(pwd)

test_under_flux 1 minimal

BLOBREF=${FLUX_BUILD_DIR}/t/kvs/blobref
RPC=${FLUX_BUILD_DIR}/t/request/rpc

SIZES="0 1 64 100 1000 1024 1025 8192 65536 262144 1048576 4194304"
LARGE_SIZES="8388608 10000000 16777216 33554432 67108864"

##
# Functions used by tests
##

# Usage: backing_load blobref
backing_load() {
        echo -n $1 | $RPC content-backing.load
}
# Usage: backing_store <blob >blobref
backing_store() {
        $RPC -r content-backing.store
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
# Leaves behind blob.<size> and blobref.<size>
check_blob() {
	make_blob $1 >blob.$1 &&
	backing_store <blob.$1 >blobref.$1 &&
	backing_load $(cat blobref.$1) >blob.$1.check &&
	test_cmp blob.$1 blob.$1.check
}
# Usage: check_blob size
# Relies on existence of blob.<size> and blobref.<size>
recheck_blob() {
	backing_load $(cat blobref.$1) >blob.$1.recheck &&
	test_cmp blob.$1 blob.$1.recheck
}
# Usage: recheck_cache_blob size
# Relies on existence of blob.<size> and blobref.<size>
recheck_cache_blob() {
	flux content load $(cat blobref.$1) >blob.$1.cachecheck &&
	test_cmp blob.$1 blob.$1.cachecheck
}
# Usage: kvs_checkpoint_put key value
kvs_checkpoint_put() {
        jq -j -c -n  "{key:\"$1\",value:\"$2\"}" | $RPC kvs-checkpoint.put
}
# Usage: kvs_checkpoint_get key >value
kvs_checkpoint_get() {
        jq -j -c -n  "{key:\"$1\"}" | $RPC kvs-checkpoint.get
}

##
# Tests of the module by itself (no content cache)
##

test_expect_success 'create creds.toml from env' '
	mkdir -p creds &&
	cat >creds/creds.toml <<-CREDS
	access-key-id = "$S3_ACCESS_KEY_ID"
	secret-access-key = "$S3_SECRET_ACCESS_KEY"
	CREDS
'

test_expect_success 'create content-s3.toml from env' '
	cat >content-s3.toml <<-TOML
	[content-s3]
	credential-file = "$(pwd)/creds/creds.toml"
	uri = "http://$S3_HOSTNAME"
	bucket = "$S3_BUCKET"
	virtual-host-style = false
	TOML
'

test_expect_success 'reload broker config' '
	flux config reload
'

test_expect_success 'load content-s3 module' '
	flux module load content-s3
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

test_expect_success HAVE_JQ 'kvs-checkpoint.put foo=bar' '
        kvs_checkpoint_put foo bar
'

test_expect_success HAVE_JQ 'kvs-checkpoint.get foo returned bar' '
        echo bar >value.exp &&
        kvs_checkpoint_get foo | jq -r .value >value.out &&
        test_cmp value.exp value.out
'

test_expect_success HAVE_JQ 'kvs-checkpoint.put updates foo=baz' '
        kvs_checkpoint_put foo baz
'

test_expect_success HAVE_JQ 'kvs-checkpoint.get foo returned baz' '
        echo baz >value2.exp &&
        kvs_checkpoint_get foo | jq -r .value >value2.out &&
        test_cmp value2.exp value2.out
'

test_expect_success 'reload content-s3 module' '
	flux module reload content-s3
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

test_expect_success HAVE_JQ 'kvs-checkpoint.get foo returns same value' '
        kvs_checkpoint_get foo | jq -r .value >value3.out &&
        test_cmp value2.exp value3.out
'

test_expect_success 'config: reload config does not take effect immediately' '
	flux config reload 2>/dev/null &&
	flux dmesg | grep content-s3 | tail -1 >reload.log &&
	grep "changes will not take effect until next flux restart" reload.log
'

test_expect_success 'save good config' '
	cp content-s3.toml content-s3.save &&
	cp creds/creds.toml creds/creds.save
'

test_expect_success 'config: reload with extra field fails' '
	cp content-s3.save content-s3.toml &&
	echo "extrafield = \"failure\"" >>content-s3.toml &&
	test_must_fail flux config reload
'

test_expect_success 'config: reload with malformed uri fails' '
	cp content-s3.save content-s3.toml &&
	sed -i -e "s/uri =.*$/uri = \"baduri\"/" \
		content-s3.toml &&
	test_must_fail flux config reload
'

test_expect_success 'config: reload with bad credential path fails' '
	cp content-s3.save content-s3.toml &&
	sed -i -e "s/credential-file =.*$/credential-file = \"nocreds\"/" \
		content-s3.toml &&
	test_must_fail flux config reload
'

test_expect_success 'config: unload module' '
	flux module remove content-s3
'

test_expect_success 'config: module fails to load without config file' '
	rm -f content-s3.toml &&
	cp creds/creds.save creds/creds.toml &&
	test_must_fail flux config reload
	flux config reload &&
	test_must_fail flux module load content-s3
'

test_expect_success 'config: module fails to load without credentials file' '
	rm -f creds/creds.toml &&
	cp content-s3.save content-s3.toml &&
	test_must_fail flux config reload
	flux config reload &&
	test_must_fail flux module load content-s3
'

test_expect_success 'config: restore good config' '
	mv -f content-s3.save content-s3.toml &&
	mv -f creds/creds.save creds/creds.toml &&
	flux config reload
'

test_expect_success 'config: load module' '
	flux module load content-s3
'

##
# Tests of the module acting as backing store for content cache
##

test_expect_success 'verify content.backing-module=content-s3' '
        test "$(flux getattr content.backing-module)" = "content-s3"
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

test_expect_success 'remove content-s3 module' '
	flux module remove content-s3
'

test_done
