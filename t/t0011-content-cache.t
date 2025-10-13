#!/bin/sh

test_description='Test broker content service'

. `dirname $0`/sharness.sh

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} minimal
echo "# $0: flux session size will be ${SIZE}"

BLOBREF=${FLUX_BUILD_DIR}/t/kvs/blobref
RPC=${FLUX_BUILD_DIR}/t/request/rpc
SPAMUTIL=${FLUX_BUILD_DIR}/t/kvs/content-spam
MAXBLOB=1048576

test_expect_success 'load content module' '
	flux exec flux module load content blob-size-limit=$MAXBLOB
'

test_expect_success 'content flush fails with ENOSYS with no backing store' '
	test_must_fail flux content flush 2> flush.err &&
	grep "Function not implemented" flush.err
'

HASHFUN=`flux getattr content.hash`

register_backing() {
	jq -j -c -n  "{name:\"$1\"}" | $RPC content.register-backing
}
unregister_backing() {
	jq -j -c -n  "{name:\"$1\"}" | $RPC content.unregister-backing
}

test_expect_success 'flux module stats content works on leader' '
	flux module stats content
'
test_expect_success 'flux module stats content works on follower' '
	flux exec -r 1 flux module stats content
'
test_expect_success 'register-backing name=foo works' '
	register_backing foo
'
test_expect_success 'register-backing name=bar fails' '
	test_must_fail register_backing bar 2>bar.err &&
	grep "already active" bar.err
'
test_expect_success 'unregister-backing name=foo works' '
	unregister_backing foo
'
test_expect_success 'register-backing name=bar fails' '
	test_must_fail register_backing bar 2>foo.err &&
	grep "cannot be changed" foo.err
'
test_expect_success 'unregister-backing name=foo fails' '
	test_must_fail unregister_backing foo 2>foo2.err &&
	grep "is not active" foo2.err
'

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

test_expect_success "cannot store blob that exceeds max size of $MAXBLOB" '
	dd if=/dev/zero count=$(($MAXBLOB/4096+1)) bs=4096 \
			skip=$(($MAXBLOB/4096)) >toobig 2>/dev/null &&
	test_must_fail flux content store <toobig
'

test_expect_success 'load and verify 0b blob on all ranks' '
	HASHSTR=`cat 0.0.hash` &&
	flux exec -n echo ${HASHSTR} >0.0.all.expect &&
	flux exec -n sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
						>0.0.all.output &&
	test_cmp 0.0.all.expect 0.0.all.output
'

test_expect_success 'load and verify 64b blob on all ranks' '
	HASHSTR=`cat 64.0.hash` &&
	flux exec -n echo ${HASHSTR} >64.0.all.expect &&
	flux exec -n sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
						>64.0.all.output &&
	test_cmp 64.0.all.expect 64.0.all.output
'

test_expect_success 'load and verify 4k blob on all ranks' '
	HASHSTR=`cat 4k.0.hash` &&
	flux exec -n echo ${HASHSTR} >4k.0.all.expect &&
	flux exec -n sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
						>4k.0.all.output &&
	test_cmp 4k.0.all.expect 4k.0.all.output
'

test_expect_success 'load and verify 1m blob on all ranks' '
	HASHSTR=`cat 1m.0.hash` &&
	flux exec -n echo ${HASHSTR} >1m.0.all.expect &&
	flux exec -n sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
						>1m.0.all.output &&
	test_cmp 1m.0.all.expect 1m.0.all.output
'

# Write on rank 3, various size blobs
# Verify on all ranks

test_expect_success 'store blobs on rank 3' '
	dd if=/dev/urandom count=1 bs=64 >64.3.store 2>/dev/null &&
	flux exec -n --rank 3 sh -c "flux content store <64.3.store >64.3.hash" &&
	dd if=/dev/urandom count=1 bs=4096 >4k.3.store 2>/dev/null &&
	flux exec -n --rank 3 sh -c "flux content store <4k.3.store >4k.3.hash" &&
	dd if=/dev/urandom count=256 bs=4096 >1m.3.store 2>/dev/null &&
	flux exec -n --rank 3 sh -c "flux content store <1m.3.store >1m.3.hash"
'

test_expect_success 'load and verify 64b blob on all ranks' '
	HASHSTR=`cat 64.3.hash` &&
	flux exec -n echo ${HASHSTR} >64.3.all.expect &&
	flux exec -n sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
						>64.3.all.output &&
	test_cmp 64.3.all.expect 64.3.all.output
'

test_expect_success 'load and verify 4k blob on all ranks' '
	HASHSTR=`cat 4k.3.hash` &&
	flux exec -n echo ${HASHSTR} >4k.3.all.expect &&
	flux exec -n sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
						>4k.3.all.output &&
	test_cmp 4k.3.all.expect 4k.3.all.output
'

test_expect_success 'load and verify 1m blob on all ranks' '
	HASHSTR=`cat 1m.3.hash` &&
	flux exec -n echo ${HASHSTR} >1m.3.all.expect &&
	flux exec -n sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
						>1m.3.all.output &&
	test_cmp 1m.3.all.expect 1m.3.all.output
'

# Simulate a lookup failure on all ranks
# Store the thing we tried to look up so it should no longer fail
# Verify that it can be retrieved on all ranks

test_expect_success 'negative entries are not cached' '
	VALUESTR=sdflskdjflsdkjfsdjf &&
	HASHSTR=`echo $VALUESTR | $BLOBREF $HASHFUN` &&
	test_must_fail flux exec -n flux content load ${HASHSTR} 2>/dev/null &&
	echo $VALUESTR | flux content store >/dev/null &&
	flux exec -n flux content load ${HASHSTR} >/dev/null
'

# Store the same content on all ranks
# Retrieve it from one rank
# (Really we want to test whether stores were squashed, fill in later)

test_expect_success 'store on all ranks can be retrieved from rank 0' '
	flux exec -n sh -c "echo foof | flux content store" >/dev/null &&
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
	${SPAMUTIL} 8192 1024 >/dev/null
'

# Write 1024 blobs per rank
test_expect_success 'store 1K blobs from all ranks using async RPC' '
	flux exec -n ${SPAMUTIL} 1024 256 >/dev/null
'

test_expect_success 'load request with empty payload fails with EPROTO(71)' '
	${RPC} content.load 71 </dev/null
'
test_expect_success 'register-backing request with empty payload fails with EPROTO(71)' '
	${RPC} content.register-backing 71 </dev/null
'
test_expect_success 'content store --chunksize option splits blobs' '
	echo "0123456789" >split.data &&
	flux content store --chunksize=8 <split.data >split.refs &&
	test $(wc -l <split.refs) -eq 2
'
test_expect_success 'content load joins multiple blobrefs on stdin' '
	flux content load <split.refs >split.out &&
	test_cmp split.data split.out
'
test_expect_success 'content load joins multiple blobrefs on command line' '
	flux content load $(cat split.refs) >split2.out &&
	test_cmp split.data split2.out
'
test_expect_success 'content store --chunksize=0 does not split blobs' '
	flux content store --chunksize=0 <split.data >split3.refs &&
	test $(wc -l <split3.refs) -eq 1 &&
	flux content load <split3.refs >split3.out &&
	test_cmp split.data split3.out
'
test_expect_success 'content store --chunksize=-1 fails' '
	test_must_fail flux content store --chunksize=-1 </dev/null
'
test_expect_success 'content load with no blobrefs fails' '
	test_must_fail flux content load </dev/null
'

test_expect_success 'remove content module' '
	flux exec flux module remove content
'
test_expect_success 'module fails to load with unknown option' '
	test_must_fail flux module load content badopt
'

test_done
