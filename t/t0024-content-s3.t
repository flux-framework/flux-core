#!/bin/sh

test_description='Test content-s3 backing store service'

. `dirname $0`/sharness.sh

if test -z "$S3_HOSTNAME"; then
	skip_all='skipping content-s3 test since S3_HOSTNAME not set'
	test_done
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

# Usage: backing_load <hash
backing_load() {
        $RPC -r content-backing.load
}
# Usage: backing_store <blob >hash
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
# Usage: checkpoint_put key rootref
checkpoint_put() {
        o="{key:\"$1\",value:{version:1,rootref:\"$2\",timestamp:2.2}}"
        jq -j -c -n  ${o} | $RPC content-backing.checkpoint-put
}
# Usage: checkpoint_get key >value
checkpoint_get() {
        jq -j -c -n  "{key:\"$1\"}" | $RPC content-backing.checkpoint-get
}

##
# Tests of the module by itself (no content cache)
##

test_expect_success 'content-s3 module load fails with unknown option' '
	test_must_fail flux module load content-s3 notoption
'
test_expect_success 'content-s3 module load fails with truncate option' '
	test_must_fail flux module load content-s3 truncate
'

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


test_expect_success HAVE_JQ 'checkpoint-put foo w/ rootref bar' '
	checkpoint_put foo bar
'

test_expect_success HAVE_JQ 'checkpoint-get foo returned rootref bar' '
        echo bar >rootref.exp &&
        checkpoint_get foo | jq -r .value | jq -r .rootref >rootref.out &&
        test_cmp rootref.exp rootref.out
'

# use grep instead of compare, incase of floating point rounding
test_expect_success HAVE_JQ 'checkpoint-get foo returned correct timestamp' '
        checkpoint_get foo | jq -r .value | jq -r .timestamp >timestamp.out &&
        grep 2.2 timestamp.out
'

test_expect_success HAVE_JQ 'checkpoint-put updates foo rootref to baz' '
        checkpoint_put foo baz
'

test_expect_success HAVE_JQ 'checkpoint-get foo returned rootref baz' '
        echo baz >rootref2.exp &&
        checkpoint_get foo | jq -r .value | jq -r .rootref >rootref2.out &&
        test_cmp rootref2.exp rootref2.out
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

test_expect_success HAVE_JQ 'checkpoint-get foo still returns rootref baz' '
        echo baz >rootref3.exp &&
        checkpoint_get foo | jq -r .value | jq -r .rootref >rootref3.out &&
        test_cmp rootref3.exp rootref3.out
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

##
# Tests of kvs checkpointing
##

test_expect_success 'generate rc1/rc3 for content-s3 backing' '
	cat >rc1-content-s3 <<EOF &&
#!/bin/bash -e
flux module load content-s3
flux module load kvs
EOF
	cat >rc3-content-s3 <<EOF &&
#!/bin/bash -e
flux module remove kvs
flux module remove content-s3
EOF
        chmod +x rc1-content-s3 &&
        chmod +x rc3-content-s3
'

test_expect_success 'run instance with content-s3 module loaded' '
	flux start -o,--setattr=broker.rc1_path=$(pwd)/rc1-content-s3 \
                   -o,--setattr=broker.rc3_path=$(pwd)/rc3-content-s3 \
	           flux kvs put testkey=43
'

test_expect_success 're-run instance with content-s3 module loaded' '
	flux start -o,--setattr=broker.rc1_path=$(pwd)/rc1-content-s3 \
                   -o,--setattr=broker.rc3_path=$(pwd)/rc3-content-s3 \
	           flux kvs get testkey >gets3.out
'

test_expect_success 'content from previous instance survived (s3)' '
	echo 43 >gets3.exp &&
	test_cmp gets3.exp gets3.out
'

test_expect_success 're-run instance, verify checkpoint date saved (s3)' '
	flux start -o,--setattr=broker.rc1_path=$(pwd)/rc1-content-s3 \
                   -o,--setattr=broker.rc3_path=$(pwd)/rc3-content-s3 \
	           flux dmesg >dmesgs3.out
'

# just check for todays date, not time for obvious reasons
test_expect_success 'verify date in flux logs (s3)' '
	today=`date --iso-8601` &&
	grep checkpoint dmesgs3.out | grep ${today}
'

test_done
