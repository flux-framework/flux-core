#!/bin/sh

test_description='Test content-sqlite service' 

. `dirname $0`/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} minimal
echo "# $0: flux session size will be ${SIZE}"

BLOBREF=${FLUX_BUILD_DIR}/t/kvs/blobref

MAXBLOB=`flux getattr content-blob-size-limit`
HASHFUN=`flux getattr content-hash`


store_junk() {
    local name=$1
    local n=$2
    for i in `seq 1 $n`; do \
        echo "$name:$i" | flux content store >/dev/null || return 1
    done
}

test_expect_success 'load content-sqlite module on rank 0' '
	flux module load --rank 0 content-sqlite
'

test_expect_success 'store 100 blobs on rank 0' '
	store_junk test 100 &&
        TOTAL=`flux comms-stats --type int --parse count content` &&
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

test_expect_success LONGTEST "cannot store blob that exceeds max size of $MAXBLOB" '
        dd if=/dev/zero count=$(($MAXBLOB/4096+1)) bs=4096 \
			skip=$(($MAXBLOB/4096)) >toobig 2>/dev/null &&
        test_must_fail flux content store --bypass-cache <toobig
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
# forcing content to fault in from the content-backing service

test_expect_success 'load and verify 64b blob on all ranks' '
        HASHSTR=`cat 64.0.hash` &&
        flux exec echo ${HASHSTR} >64.0.all.expect &&
        flux exec sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
                                                >64.0.all.output &&
        test_cmp 64.0.all.expect 64.0.all.output
'

test_expect_success 'load and verify 4k blob on all ranks' '
        HASHSTR=`cat 4k.0.hash` &&
        flux exec echo ${HASHSTR} >4k.0.all.expect &&
        flux exec sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
                                                >4k.0.all.output &&
        test_cmp 4k.0.all.expect 4k.0.all.output
'

test_expect_success 'load and verify 1m blob on all ranks' '
        HASHSTR=`cat 1m.0.hash` &&
        flux exec echo ${HASHSTR} >1m.0.all.expect &&
        flux exec sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
                                                >1m.0.all.output &&
        test_cmp 1m.0.all.expect 1m.0.all.output
'

# Flush any pending stores and drop cache
# Unload persistence module
# Verify content is not lost

test_expect_success 'flush rank 0 cache' '
        run_timeout 10 flux content flush &&
	NDIRTY=`flux comms-stats --type int --parse dirty content` &&
	test $NDIRTY -eq 0
'

test_expect_success 'drop rank 0 cache' '
        flux content dropcache &&
	ECOUNT=`flux comms-stats --type int --parse count content` &&
	test $ECOUNT -eq 0
'

test_expect_success 'unload content-sqlite module' '
	flux module remove --rank 0 content-sqlite
'

test_expect_success 'check that content returned dirty' '
	NDIRTY=`flux comms-stats --type int --parse dirty content` &&
	ECOUNT=`flux comms-stats --type int --parse count content` &&
	test $NDIRTY -eq $ECOUNT
'

test_expect_success 'load 64b blob from cache' '
        HASHSTR=`cat 64.0.hash` &&
        flux content load ${HASHSTR} >64.0.load2 &&
        test_cmp 64.0.store 64.0.load2
'

test_expect_success 'load 4k blob from cache' '
        HASHSTR=`cat 4k.0.hash` &&
        flux content load ${HASHSTR} >4k.0.load2 &&
        test_cmp 4k.0.store 4k.0.load2
'

test_expect_success 'load 1m blob' '
        HASHSTR=`cat 1m.0.hash` &&
        flux content load ${HASHSTR} >1m.0.load2 &&
        test_cmp 1m.0.store 1m.0.load2
'

# Re-load persistence module
# Verify content is transferred to store

test_expect_success 'load content-sqlite module on rank 0' '
	flux module load --rank 0 content-sqlite
'

test_expect_success 'flush rank 0 cache' '
        run_timeout 10 flux content flush &&
	NDIRTY=`flux comms-stats --type int --parse dirty content` &&
	test $NDIRTY -eq 0
'

test_expect_success 'load 64b blob bypassing cache' '
        HASHSTR=`cat 64.0.hash` &&
        flux content load --bypass-cache ${HASHSTR} >64.0.load3 &&
        test_cmp 64.0.store 64.0.load3
'

test_expect_success 'load 4k blob bypassing cache' '
        HASHSTR=`cat 4k.0.hash` &&
        flux content load --bypass-cache ${HASHSTR} >4k.0.load3 &&
        test_cmp 4k.0.store 4k.0.load3
'

test_expect_success 'load 1m blob bypassing cache' '
        HASHSTR=`cat 1m.0.hash` &&
        flux content load --bypass-cache ${HASHSTR} >1m.0.load3 &&
        test_cmp 1m.0.store 1m.0.load3
'

test_expect_success 'exercise batching of synchronous flush to backing store' '
	flux setattr content-flush-batch-limit 5 &&
        store_junk loadunload 200 &&
    	flux content flush &&
	NDIRTY=`flux comms-stats --type int --parse dirty content` &&
	test ${NDIRTY} -eq 0
'

test_expect_success 'exercise batching of asynchronous flush to backing store' '
        OLD_COUNT=`flux comms-stats --type int --parse count content` &&
	flux module remove --rank 0 content-sqlite &&
	flux module load --rank 0 content-sqlite &&
	flux module remove --rank 0 content-sqlite &&
	flux module load --rank 0 content-sqlite &&
	flux module remove --rank 0 content-sqlite &&
	flux module load --rank 0 content-sqlite &&
        NEW_COUNT=`flux comms-stats --type int --parse count content` &&
	test $OLD_COUNT -le $NEW_COUNT
'


test_done
