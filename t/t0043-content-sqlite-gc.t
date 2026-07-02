#!/bin/sh

test_description='Test content-sqlite garbage collection primitives

Exercise the content-backing.gc-info, .mark, and .sweep RPCs and the
epoch stamping done by content-backing.store, directly and independent
of the flux-gc tool.  The safety of online GC rests entirely on these
primitives behaving exactly (mark is monotonic, sweep deletes only
below the threshold, the epoch tracks checkpoints), so they are pinned
here at the RPC level.'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

test_under_flux 1 minimal

RPC=${FLUX_BUILD_DIR}/t/request/rpc
BLOBREF=${FLUX_BUILD_DIR}/t/kvs/blobref

test_expect_success 'load content and content-sqlite modules' '
	flux module load content &&
	flux module load content-sqlite
'

HASHFUN=$(flux getattr content.hash)

# store_blob CONTENT -> prints blobref (stored directly to backing store)
store_blob() {
	printf "%s" "$1" | flux content store --bypass-cache
}
# gc_info THRESHOLD -> prints response JSON {current_epoch, candidates}
gc_info() {
	echo "{\"epoch\":$1}" | $RPC content-backing.gc-info
}
# mark_blob EPOCH BLOBREF -> prints response JSON {marked}
mark_blob() {
	echo "{\"epoch\":$1,\"hashes\":[\"$2\"]}" | $RPC content-backing.mark
}
# sweep EPOCH BATCHSIZE -> prints response JSON {deleted, remaining}
sweep() {
	echo "{\"epoch\":$1,\"batch_size\":$2}" | $RPC content-backing.sweep
}

#
# gc-info and epoch stamping
#

test_expect_success 'gc-info reports current_epoch 0 on fresh store' '
	test $(gc_info 1 | jq .current_epoch) -eq 0
'
test_expect_success 'gc-info reports 0 candidates on empty store' '
	test $(gc_info 1000 | jq .candidates) -eq 0
'
test_expect_success 'store three blobs, stamped at epoch 0' '
	store_blob content-A >A.ref &&
	store_blob content-B >B.ref &&
	store_blob content-C >C.ref
'
test_expect_success 'gc-info counts all three below epoch 1' '
	test $(gc_info 1 | jq .candidates) -eq 3
'
test_expect_success 'checkpoint-put advances the epoch to 1' '
	checkpoint_put root1 &&
	test $(gc_info 1 | jq .current_epoch) -eq 1
'
test_expect_success 'module stats reports current_epoch matching gc-info' '
	test $(flux module stats content-sqlite | jq .current_epoch) -eq \
	     $(gc_info 1 | jq .current_epoch)
'
test_expect_success 'blobs stored after a checkpoint are stamped at epoch 1' '
	store_blob content-D >D.ref &&
	store_blob content-E >E.ref &&
	test $(gc_info 1 | jq .candidates) -eq 3 &&
	test $(gc_info 2 | jq .candidates) -eq 5
'

#
# mark
#

test_expect_success 'mark of an existing blob reports marked=1' '
	test $(mark_blob 10 $(cat A.ref) | jq .marked) -eq 1
'
test_expect_success 'mark moved the blob above the threshold' '
	test $(gc_info 1 | jq .candidates) -eq 2
'
test_expect_success 'mark is monotonic: a lower epoch does not lower the blob' '
	test $(mark_blob 5 $(cat A.ref) | jq .marked) -eq 1 &&
	test $(gc_info 6 | jq .candidates) -eq 4
'
test_expect_success 'mark of a non-existent blob reports marked=0' '
	printf absent | $BLOBREF $HASHFUN >absent.ref &&
	test $(mark_blob 10 $(cat absent.ref) | jq .marked) -eq 0
'
test_expect_success 'mark of an over-limit batch fails with a useful message' '
	ref=$(cat A.ref) &&
	hashes=$(jq -nc --arg r "$ref" "[range(16385) | \$r]") &&
	echo "{\"epoch\":1,\"hashes\":$hashes}" >big.json &&
	test_must_fail $RPC content-backing.mark <big.json 2>big.err &&
	grep "exceeds limit" big.err
'

#
# sweep
#

test_expect_success 'sweep below epoch 1 deletes only the epoch-0 blobs' '
	sweep 1 100 >sweep1.out &&
	test $(jq .deleted <sweep1.out) -eq 2 &&
	test $(jq .remaining <sweep1.out) -eq 0
'
test_expect_success 'swept blobs are gone, others remain' '
	test_must_fail flux content load --bypass-cache $(cat B.ref) &&
	test_must_fail flux content load --bypass-cache $(cat C.ref) &&
	flux content load --bypass-cache $(cat A.ref) >/dev/null &&
	flux content load --bypass-cache $(cat D.ref) >/dev/null &&
	flux content load --bypass-cache $(cat E.ref) >/dev/null
'
test_expect_success 'sweep above all epochs empties the store' '
	sweep 11 100 >sweepall.out &&
	test $(jq .deleted <sweepall.out) -eq 3 &&
	test $(jq .remaining <sweepall.out) -eq 0 &&
	test $(gc_info 1000 | jq .candidates) -eq 0
'
test_expect_success 'sweep honors batch_size and reports remaining' '
	store_blob content-F >/dev/null &&
	store_blob content-G >/dev/null &&
	store_blob content-H >/dev/null &&
	store_blob content-I >/dev/null &&
	sweep 2 2 >batch1.out &&
	test $(jq .deleted <batch1.out) -eq 2 &&
	test $(jq .remaining <batch1.out) -eq 2 &&
	sweep 2 2 >batch2.out &&
	test $(jq .deleted <batch2.out) -eq 2 &&
	test $(jq .remaining <batch2.out) -eq 0
'
test_expect_success 'gc-info candidate count matches what a sweep would delete' '
	store_blob content-X >/dev/null &&
	store_blob content-Y >/dev/null &&
	candidates=$(gc_info 2 | jq .candidates) &&
	test ${candidates} -eq 2 &&
	test $(sweep 2 100 | jq .deleted) -eq ${candidates}
'

#
# store re-stamps epoch on dedup (ON CONFLICT DO UPDATE)
#

test_expect_success 're-storing a blob after a checkpoint refreshes its epoch' '
	store_blob content-J >J.ref &&
	store_blob content-K >K.ref &&
	checkpoint_put root2 &&
	test $(gc_info 2 | jq .current_epoch) -eq 2 &&
	store_blob content-J >J2.ref &&
	test_cmp J.ref J2.ref
'
test_expect_success 'sweep below the new epoch keeps the re-stamped blob, drops the stale one' '
	sweep 2 100 >restamp.out &&
	flux content load --bypass-cache $(cat J.ref) >/dev/null &&
	test_must_fail flux content load --bypass-cache $(cat K.ref)
'

#
# flux-gc with the KVS module not loaded
#
# The kvs module is never loaded in this test (only content + content-sqlite),
# so flux-gc cannot enumerate live private namespace or primary roots.  The
# design doc states this is safe: GC marks from the latest checkpoint alone,
# mirroring flux-dump --checkpoint.  Verify the tool degrades gracefully rather
# than aborting when kvs.namespace-list / kvs.getroot return ENOSYS.
#
# Reload with truncate first: the checkpoints stored above use placeholder
# rootrefs that are not loadable treeobjs, which flux-gc's mark phase would
# (correctly) choke on.  Starting fresh gives us a single real checkpoint root.

test_expect_success 'reload content-sqlite truncated for a clean checkpoint table' '
	flux module reload content-sqlite truncate
'
test_expect_success 'store an empty-dir treeobj and garbage at epoch 0' '
	printf "{\"ver\":1,\"type\":\"dir\",\"data\":{}}" \
	    | flux content store --bypass-cache >gcroot.ref &&
	store_blob content-gc-garbage1 >gcg1.ref &&
	store_blob content-gc-garbage2 >gcg2.ref
'
test_expect_success 'checkpoint the real root, advancing the epoch above the garbage' '
	checkpoint_put $(cat gcroot.ref) &&
	test $(gc_info 1 | jq .current_epoch) -eq 1
'
test_expect_success "flux gc succeeds with kvs not loaded" '
	flux gc --verbose >gc-nokvs.out 2>gc-nokvs.err &&
	grep -i "kvs not loaded" gc-nokvs.err
'
test_expect_success "flux gc reclaimed the unreferenced blobs" '
	test_must_fail flux content load --bypass-cache $(cat gcg1.ref) &&
	test_must_fail flux content load --bypass-cache $(cat gcg2.ref)
'
test_expect_success "flux gc preserved the checkpointed root" '
	flux content load --bypass-cache $(cat gcroot.ref) >/dev/null
'

test_expect_success 'remove content-sqlite and content modules' '
	flux module remove content-sqlite &&
	flux module remove content
'

test_done
