#!/bin/sh

test_description='Test broker content service' 

. `dirname $0`/sharness.sh

if test "$TEST_LONG" = "t"; then
    test_set_prereq LONGTEST
fi

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} minimal
echo "# $0: flux session size will be ${SIZE}"

BLOBREF=${FLUX_BUILD_DIR}/t/kvs/blobref

MAXBLOB=`flux getattr content.blob-size-limit`
HASHFUN=`flux getattr content.hash`

test_expect_success 'store 100 blobs on rank 0' '
        for i in `seq 0 99`; do echo test$i | \
            flux content store >/dev/null; done &&
	TOTAL=`flux module stats --type int --parse count content` &&
        test $TOTAL -ge 100 
'

# Write on rank 0, various size blobs
# Verify on all ranks

test_expect_success 'store test blobs on rank 0' '
	cat /dev/null >0.0.store 2>/dev/null &&
	flux content store <0.0.store >0.0.hash &&
	dd if=/dev/urandom count=1 bs=64 >64.0.store 2>/dev/null &&
	flux content store <64.0.store >64.0.hash &&
	dd if=/dev/urandom count=1 bs=4096 >4k.0.store 2>/dev/null &&
	flux content store <4k.0.store >4k.0.hash &&
	dd if=/dev/urandom count=256 bs=4096 >1m.0.store 2>/dev/null &&
	flux content store <1m.0.store >1m.0.hash
'

test_expect_success LONGTEST "cannot store blob that exceeds max size of $MAXBLOB" '
	dd if=/dev/zero count=$(($MAXBLOB/4096+1)) bs=4096 \
			skip=$(($MAXBLOB/4096)) >toobig 2>/dev/null &&
	test_must_fail flux content store <toobig
'

test_expect_success 'load and verify 0b blob on all ranks' '
	HASHSTR=`cat 0.0.hash` &&
	flux exec echo ${HASHSTR} >0.0.all.expect &&
	flux exec sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
						>0.0.all.output &&
	test_cmp 0.0.all.expect 0.0.all.output
'

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

# Write on rank 3, various size blobs
# Verify on all ranks

test_expect_success 'store blobs on rank 3' '
	dd if=/dev/urandom count=1 bs=64 >64.3.store 2>/dev/null &&
	flux exec --rank 3 sh -c "flux content store <64.3.store >64.3.hash" &&
	dd if=/dev/urandom count=1 bs=4096 >4k.3.store 2>/dev/null &&
	flux exec --rank 3 sh -c "flux content store <4k.3.store >4k.3.hash" &&
	dd if=/dev/urandom count=256 bs=4096 >1m.3.store 2>/dev/null &&
	flux exec --rank 3 sh -c "flux content store <1m.3.store >1m.3.hash"
'

test_expect_success 'load and verify 64b blob on all ranks' '
	HASHSTR=`cat 64.3.hash` &&
	flux exec echo ${HASHSTR} >64.3.all.expect &&
	flux exec sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
						>64.3.all.output &&
	test_cmp 64.3.all.expect 64.3.all.output
'

test_expect_success 'load and verify 4k blob on all ranks' '
	HASHSTR=`cat 4k.3.hash` &&
	flux exec echo ${HASHSTR} >4k.3.all.expect &&
	flux exec sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
						>4k.3.all.output &&
	test_cmp 4k.3.all.expect 4k.3.all.output
'

test_expect_success 'load and verify 1m blob on all ranks' '
	HASHSTR=`cat 1m.3.hash` &&
	flux exec echo ${HASHSTR} >1m.3.all.expect &&
	flux exec sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
						>1m.3.all.output &&
	test_cmp 1m.3.all.expect 1m.3.all.output
'

# Simulate a lookup failure on all ranks
# Store the thing we tried to look up so it should no longer fail
# Verify that it can be retrieved on all ranks

test_expect_success 'negative entries are not cached' '
	VALUESTR=sdflskdjflsdkjfsdjf &&
	HASHSTR=`echo $VALUESTR | $BLOBREF $HASHFUN` &&
	test_must_fail flux exec flux content load ${HASHSTR} 2>/dev/null &&
	echo $VALUESTR | flux content store >/dev/null &&
	flux exec flux content load ${HASHSTR} >/dev/null
'

# Store the same content on all ranks
# Retrieve it from one rank
# (Really we want to test whther stores were squashed, fill in later)

test_expect_success 'store on all ranks can be retrieved from rank 0' '
	flux exec sh -c "echo foof | flux content store" >/dev/null &&
	flux content load `echo foof | $BLOBREF $HASHFUN` >/dev/null
'

# Backing store is not loaded so all entries on rank 0 should be dirty
test_expect_success 'rank 0 cache is all dirty' '
	DIRTY=`flux module stats --type int --parse dirty content` &&
	TOTAL=`flux module stats --type int --parse count content` &&
	test $DIRTY -eq $TOTAL
'

# Backing store is not loaded so all entries on rank 0 should be valid
test_expect_success 'rank 0 cache is all valid' '
	VALID=`flux module stats --type int --parse valid content` &&
	TOTAL=`flux module stats --type int --parse count content` &&
	test $VALID -eq $TOTAL
'

# Write 8192 blobs, allowing 1024 requests to be outstanding
test_expect_success 'store 8K blobs from rank 0 using async RPC' '
	flux content spam 8192 1024
'

# Write 1024 blobs per rank
test_expect_success 'store 1K blobs from all ranks using async RPC' '
	flux exec flux content spam 1024 256
'

test_done
