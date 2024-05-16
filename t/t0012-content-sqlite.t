#!/bin/sh

test_description='Test content-sqlite service'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} minimal
echo "# $0: flux session size will be ${SIZE}"

PURGE_TARGET_SIZE=100
PURGE_OLD_ENTRY=1
FLUSH_BATCH_LIMIT=5

BLOBREF=${FLUX_BUILD_DIR}/t/kvs/blobref
RPC=${FLUX_BUILD_DIR}/t/request/rpc
SPAMUTIL="${FLUX_BUILD_DIR}/t/kvs/content-spam"
rc1_kvs=$SHARNESS_TEST_SRCDIR/rc/rc1-kvs
rc3_kvs=$SHARNESS_TEST_SRCDIR/rc/rc3-kvs
shutdown_kvs=$SHARNESS_TEST_SRCDIR/rc/shutdown-kvs

test_expect_success 'load content module with lower purge/age thresholds' '
	flux exec flux module load content \
	    purge-target-size=$PURGE_TARGET_SIZE \
	    purge-old-entry=$PURGE_OLD_ENTRY \
	    flush-batch-limit=$FLUSH_BATCH_LIMIT
'

HASHFUN=`flux getattr content.hash`

test_expect_success 'load heartbeat module with fast rate to drive purge' '
	flux module load heartbeat period=0.1s
'

test_expect_success 'load content-sqlite module on rank 0' '
	flux module load content-sqlite
'

test_expect_success 'verify content.backing-module=content-sqlite' '
	test "$(flux getattr content.backing-module)" = "content-sqlite"
'

test_expect_success 'store 100 blobs on rank 0' '
	${SPAMUTIL} 100 100 >/dev/null &&
	TOTAL=`flux module stats --type int --parse count content` &&
	test $TOTAL -ge 100
'

# Store directly to content service
# Verify directly from content service

test_expect_success 'store blobs bypassing cache' '
	cat /dev/null >0.0.store &&
	flux content store --bypass-cache <0.0.store >0.0.hash &&
	dd if=/dev/urandom count=1 bs=64 >64.0.store 2>/dev/null &&
	flux content store --bypass-cache <64.0.store >64.0.hash &&
	dd if=/dev/urandom count=1 bs=4096 >4k.0.store 2>/dev/null &&
	flux content store --bypass-cache <4k.0.store >4k.0.hash &&
	dd if=/dev/urandom count=256 bs=4096 >1m.0.store 2>/dev/null &&
	flux content store --bypass-cache <1m.0.store >1m.0.hash
'

test_expect_success 'load 0b blob bypassing cache' '
	HASHSTR=`cat 0.0.hash` &&
	flux content load --bypass-cache ${HASHSTR} >0.0.load &&
	test_cmp 0.0.store 0.0.load
'

test_expect_success 'load 64b blob bypassing cache' '
	HASHSTR=`cat 64.0.hash` &&
	flux content load --bypass-cache ${HASHSTR} >64.0.load &&
	test_cmp 64.0.store 64.0.load
'

test_expect_success 'load 4k blob bypassing cache' '
	HASHSTR=`cat 4k.0.hash` &&
	flux content load --bypass-cache ${HASHSTR} >4k.0.load &&
	test_cmp 4k.0.store 4k.0.load
'

test_expect_success 'load 1m blob bypassing cache' '
	HASHSTR=`cat 1m.0.hash` &&
	flux content load --bypass-cache ${HASHSTR} >1m.0.load &&
	test_cmp 1m.0.store 1m.0.load
'

# Verify same blobs on all ranks
# forcing content to fault in from the content backing service

test_expect_success 'load and verify 64b blob on all ranks' '
	HASHSTR=`cat 64.0.hash` &&
	flux exec -n echo ${HASHSTR} >64.0.all.expect &&
	flux exec -n sh -c "flux content load ${HASHSTR} \
		| $BLOBREF $HASHFUN" >64.0.all.output &&
	test_cmp 64.0.all.expect 64.0.all.output
'

test_expect_success 'load and verify 4k blob on all ranks' '
	HASHSTR=`cat 4k.0.hash` &&
	flux exec -n echo ${HASHSTR} >4k.0.all.expect &&
	flux exec -n sh -c "flux content load ${HASHSTR} \
		| $BLOBREF $HASHFUN" >4k.0.all.output &&
	test_cmp 4k.0.all.expect 4k.0.all.output
'

test_expect_success 'load and verify 1m blob on all ranks' '
	HASHSTR=`cat 1m.0.hash` &&
	flux exec -n echo ${HASHSTR} >1m.0.all.expect &&
	flux exec -n sh -c "flux content load ${HASHSTR} \
		| $BLOBREF $HASHFUN" >1m.0.all.output &&
	test_cmp 1m.0.all.expect 1m.0.all.output
'

test_expect_success 'exercise batching of synchronous flush to backing store' '
	${SPAMUTIL} 200 200 >/dev/null &&
	flux content flush &&
	NDIRTY=`flux module stats --type int --parse dirty content` &&
	test ${NDIRTY} -eq 0
'

test_expect_success 'drop the cache' '
	flux content dropcache
'

test_expect_success 'fill the cache with more data for later purging' '
	${SPAMUTIL} 10000 200 >/dev/null
'

test_expect_success 'checkpoint-put foo w/ rootref bar' '
	checkpoint_put foo bar
'

test_expect_success 'checkpoint-get foo returned rootref bar' '
	echo bar >rootref.exp &&
	checkpoint_get foo | jq -r .value | jq -r .rootref >rootref.out &&
	test_cmp rootref.exp rootref.out
'

test_expect_success 'checkpoint-put on rank 1 forwards to rank 0' '
       o=$(checkpoint_put_msg rankone rankref) &&
       jq -j -c -n ${o} | flux exec -r 1 ${RPC} content.checkpoint-put
'

test_expect_success 'checkpoint-get on rank 1 forwards to rank 0' '
       echo rankref >rankref.exp &&
       o=$(checkpoint_get_msg rankone) &&
       jq -j -c -n ${o} \
	   | flux exec -r 1 ${RPC} content.checkpoint-get \
	   | jq -r .value | jq -r .rootref > rankref.out &&
       test_cmp rankref.exp rankref.out
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

test_expect_success 'flush + reload content-sqlite module on rank 0' '
	flux content flush &&
	flux module reload content-sqlite
'

test_expect_success 'checkpoint-get foo still returns rootref baz' '
	echo baz >rootref3.exp &&
	checkpoint_get foo | jq -r .value | jq -r .rootref >rootref3.out &&
	test_cmp rootref3.exp rootref3.out
'

test_expect_success 'checkpoint-backing-get foo returns rootref baz' '
	echo baz >rootref_backing.exp &&
	checkpoint_backing_get foo \
            | jq -r .value \
            | jq -r .rootref >rootref_backing.out &&
	test_cmp rootref_backing.exp rootref_backing.out
'

test_expect_success 'checkpoint-backing-put foo w/ rootref boof' '
	checkpoint_backing_put foo boof
'

test_expect_success 'checkpoint-get foo returned rootref boof' '
	echo boof >rootref4.exp &&
	checkpoint_get foo | jq -r .value | jq -r .rootref >rootref4.out &&
	test_cmp rootref4.exp rootref4.out
'

test_expect_success 'checkpoint-get noexist fails with No such...' '
	test_must_fail checkpoint_get noexist 2>badkey.err &&
	grep "No such file or directory" badkey.err
'

test_expect_success 'content-backing.load wrong size hash fails with EPROTO' '
	echo -n xxx >badhash &&
	$RPC content-backing.load 71 <badhash 2>load.err
'

getsize() {
	flux module stats content | tee /dev/fd/2 | jq .size
}

test_expect_success 'wait for purge to clear cache entries' '
	echo "Purge size $PURGE_TARGET_SIZE bytes, age $PURGE_OLD_ENTRY secs" &&
	size=$(getsize) && \
	count=0 &&
	while test $size -gt 100 -a $count -lt 300; do \
		sleep 0.1; \
		size=$(getsize); \
		count=$(($count+1))
	done
'

test_expect_success 'remove content-sqlite module on rank 0' '
	flux content flush &&
	flux module remove content-sqlite
'

test_expect_success 'checkpoint-put foo w/ rootref spoon' '
	checkpoint_put foo spoon
'

test_expect_success 'checkpoint-get foo returned rootref spoon' '
	echo spoon >rootref5.exp &&
	checkpoint_get foo | jq -r .value | jq -r .rootref >rootref5.out &&
	test_cmp rootref5.exp rootref5.out
'

test_expect_success 'load content-sqlite module on rank 0' '
	flux module load content-sqlite
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

test_expect_success 'remove content-sqlite module on rank 0' '
	flux content flush &&
	flux module remove content-sqlite
'

test_expect_success 'remove heartbeat module' '
	flux module remove heartbeat
'

# test for issue #4210
test_expect_success 'remove read permission from content.sqlite file' '
	chmod u-w $(flux getattr rundir)/content.sqlite &&
	test_must_fail flux module load content-sqlite
'
test_expect_success 'restore read permission on content.sqlite file' '
	chmod u+w $(flux getattr rundir)/content.sqlite
'

# Clean slate for a few more tests
test_expect_success 'load content-sqlite with truncate option' '
	flux module load content-sqlite truncate
'
test_expect_success 'content-sqlite and content-cache are empty' '
	test $(flux module stats \
	    --type int --parse object_count content-sqlite) -eq 0 &&
	test $(flux module stats \
	    --type int --parse count content) -eq 0
'

test_expect_success 'storing the same object multiple times is just one row' '
	for i in $(seq 1 10); do \
	    echo foo | flux content store --bypass-cache >/dev/null; \
        done &&
	test $(flux module stats \
	    --type int --parse store_time.count content-sqlite) -eq 10 &&
	test $(flux module stats \
	    --type int --parse object_count content-sqlite) -eq 1
'
test_expect_success 'flux module reload content-sqlite' '
	flux module reload content-sqlite
'
test_expect_success 'database survives module reload' '
	test $(flux module stats \
	    --type int --parse store_time.count content-sqlite) -eq 0 &&
	test $(flux module stats \
	    --type int --parse object_count content-sqlite) -eq 1
'
test_expect_success 'reload module with bad option' '
	flux module remove content-sqlite &&
	test_must_fail flux module load content-sqlite unknown=42
'
test_expect_success 'reload module with journal_mode=WAL synchronous=NORMAL' '
	flux module remove -f content-sqlite &&
	flux dmesg --clear &&
	flux module load content-sqlite journal_mode=WAL synchronous=NORMAL &&
	flux dmesg >logs &&
	grep "journal_mode=WAL synchronous=NORMAL" logs
'
test_expect_success 'reload module with no options and verify modes' '
	flux module remove -f content-sqlite &&
	flux dmesg --clear &&
	flux module load content-sqlite &&
	flux dmesg >logs2 &&
	grep "journal_mode=OFF synchronous=OFF" logs2
'


test_expect_success 'run flux without statedir and verify modes' '
	flux start \
	    -o,-Sbroker.rc1_path=$rc1_kvs \
	    -o,-Sbroker.shutdown_path=$shutdown_kvs \
	    -o,-Sbroker.rc3_path=$rc3_kvs \
	    flux dmesg >logs3 &&
	grep "journal_mode=OFF synchronous=OFF" logs3
'
test_expect_success 'run flux with statedir and verify modes' '
	flux start \
	    -o,-Sbroker.rc1_path=$rc1_kvs \
	    -o,-Sbroker.shutdown_path=$shutdown_kvs \
	    -o,-Sbroker.rc3_path=$rc3_kvs \
	    -o,-Sstatedir=$(pwd) flux dmesg >logs4  &&
	grep "journal_mode=WAL synchronous=NORMAL" logs4
'

# Will create in WAL mode since statedir is set
recreate_database()
{
	flux start \
	    -o,-Sbroker.rc1_path= \
	    -o,-Sbroker.shutdown_path= \
	    -o,-Sbroker.rc3_path= \
	    -o,-Sstatedir=$(pwd) bash -c \
	    "flux module load content &&
	    flux module load content-sqlite truncate && \
	    flux module remove content-sqlite && \
	    flux module remove content"
}
load_module_xfail()
{
	flux start \
	    -o,-Sbroker.rc1_path= \
	    -o,-Sbroker.shutdown_path= \
	    -o,-Sbroker.rc3_path= \
	    -o,-Sstatedir=$(pwd) bash -c \
	    "flux module load content; \
	    flux module load content-sqlite; \
	    rc=\$?; \
	    flux module remove -f content-sqlite; \
	    flux module remove -f content; \
	    exit \$rc"
}

# FWIW https://www.sqlite.org/fileformat.html
test_expect_success 'create database with bad header magic' '
	recreate_database &&
	echo "xxxxxxxxxxxxxxxx" | dd obs=1 count=16 seek=0 of=content.sqlite
'
test_expect_success 'module load fails with corrupt database' '
	test_must_fail load_module_xfail
'
test_expect_success 'create database with bad schema format number' '
	recreate_database &&
	echo "\001\001\001\001" | dd obs=1 count=4 seek=44 of=content.sqlite
'
test_expect_success 'module load fails with corrupt database' '
	test_must_fail load_module_xfail
'
test_expect_success 'full instance start fails corrupt database' '
	test_must_fail flux start -o,-Sstatedir=$(pwd) /bin/true
'

test_expect_success 'flux module stats content-sqlite is open to guests' '
	FLUX_HANDLE_ROLEMASK=0x2 \
	    flux module stats content-sqlite >/dev/null
'

test_expect_success 'remove content-sqlite module on rank 0' '
	flux module remove content-sqlite
'

test_expect_success 'remove content module' '
	flux exec flux module remove content
'

test_done
