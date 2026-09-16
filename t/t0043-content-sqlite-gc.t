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
GC_RACE=${FLUX_BUILD_DIR}/t/kvs/content-gc-race

test_expect_success 'load content and content-sqlite modules' '
	flux module load content &&
	flux module load content-sqlite
'

HASHFUN=$(flux getattr content.hash)

# store_blob CONTENT -> prints blobref (stored directly to backing store)
store_blob() {
	printf "%s" "$1" | flux content store --bypass-cache
}
# gc_info THRESHOLD -> prints response JSON {current_epoch, high_water, candidates}
# (requests the optional candidate count explicitly)
gc_info() {
	echo "{\"epoch\":$1,\"get_count\":true}" | $RPC content-backing.gc-info
}
# mark_blob EPOCH BLOBREF -> prints response JSON {marked}
mark_blob() {
	echo "{\"epoch\":$1,\"hashes\":[\"$2\"]}" | $RPC content-backing.mark
}
# sweep EPOCH CURSOR HIGH_WATER DELETE_CAP WINDOW -> response JSON {deleted,cursor}
sweep() {
	echo "{\"epoch\":$1,\"cursor\":$2,\"high_water\":$3,\"delete_cap\":$4,\"window\":$5}" \
	    | $RPC content-backing.sweep
}
# sweep_all EPOCH -> total blobs deleted below EPOCH (loops the cursor to the
# frozen high-water rowid using generous caps, as flux-gc does)
sweep_all() {
	hw=$(gc_info $1 | jq .high_water)
	cursor=0
	total=0
	while test ${cursor} -lt ${hw}; do
		out=$(sweep $1 ${cursor} ${hw} 1000000 1000000) &&
		total=$(( total + $(echo "${out}" | jq .deleted) )) &&
		cursor=$(echo "${out}" | jq .cursor) || return 1
	done
	echo ${total}
}

#
# gc-info and epoch stamping
#

test_expect_success 'gc-info reports current_epoch 0 on fresh store' '
	test $(gc_info 1 | jq .current_epoch) -eq 0
'
test_expect_success 'gc-info omits candidates unless get_count is set' '
	echo "{\"epoch\":1}" | $RPC content-backing.gc-info >nocount.out &&
	test $(jq .current_epoch <nocount.out) -eq 0 &&
	test $(jq "has(\"candidates\")" <nocount.out) = false
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
	test $(sweep_all 1) -eq 2
'
test_expect_success 'swept blobs are gone, others remain' '
	test_must_fail flux content load --bypass-cache $(cat B.ref) &&
	test_must_fail flux content load --bypass-cache $(cat C.ref) &&
	flux content load --bypass-cache $(cat A.ref) >/dev/null &&
	flux content load --bypass-cache $(cat D.ref) >/dev/null &&
	flux content load --bypass-cache $(cat E.ref) >/dev/null
'
test_expect_success 'sweep above all epochs empties the store' '
	test $(sweep_all 11) -eq 3 &&
	test $(gc_info 1000 | jq .candidates) -eq 0
'
test_expect_success 'sweep delete_cap bounds deletes per call and advances the cursor' '
	store_blob content-F >/dev/null &&
	store_blob content-G >/dev/null &&
	store_blob content-H >/dev/null &&
	store_blob content-I >/dev/null &&
	hw=$(gc_info 2 | jq .high_water) &&
	sweep 2 0 ${hw} 2 1000000 >batch1.out &&
	test $(jq .deleted <batch1.out) -eq 2 &&
	test $(gc_info 2 | jq .candidates) -eq 2 &&
	c=$(jq .cursor <batch1.out) &&
	sweep 2 ${c} ${hw} 2 1000000 >batch2.out &&
	test $(jq .deleted <batch2.out) -eq 2 &&
	test $(gc_info 2 | jq .candidates) -eq 0
'
test_expect_success 'sweep window bounds the scan and advances the cursor by the window' '
	store_blob content-W >/dev/null &&
	hw=$(gc_info 3 | jq .high_water) &&
	sweep 3 0 ${hw} 1000000 1 >win.out &&
	test $(jq .cursor <win.out) -eq 1
'
test_expect_success 'sweep clamps an over-limit delete_cap rather than failing' '
	store_blob content-cap1 >/dev/null &&
	store_blob content-cap2 >/dev/null &&
	hw=$(gc_info 3 | jq .high_water) &&
	sweep 3 0 ${hw} 1000000000 1000000000 >clamp.out &&
	test $(jq .deleted <clamp.out) -eq 2
'
test_expect_success 'gc-info candidate count matches what a sweep would delete' '
	store_blob content-X >/dev/null &&
	store_blob content-Y >/dev/null &&
	candidates=$(gc_info 3 | jq .candidates) &&
	test ${candidates} -eq 2 &&
	test $(sweep_all 3) -eq ${candidates}
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
	test_cmp J.ref J2.ref &&
	test $(gc_info 2 | jq .candidates) -eq 1 &&
	test $(gc_info 3 | jq .candidates) -eq 2
'
test_expect_success 'sweep below the new epoch keeps the re-stamped blob, drops the stale one' '
	test $(sweep_all 2) -eq 1 &&
	test "$(flux content load --bypass-cache $(cat J.ref))" = "content-J" &&
	test_must_fail flux content load --bypass-cache $(cat K.ref)
'

#
# mark/sweep must flush an open group-commit batch
#
# Group commit holds a sqlite transaction open across a store burst; mark and
# sweep open their own transaction and must flush that batch first (no nested
# transactions, and the GC pass must see committed state).  content-gc-race
# pipelines a store burst immediately followed by a mark/sweep on one handle,
# so the GC RPC lands while the batch is still open -- something the one-shot
# rpc tool cannot reproduce.  Run with a short batch timeout so the batch does
# not simply age out before the GC request is processed.
#

test_expect_success 'reload content-sqlite with a long batch timeout' '
	flux module reload content-sqlite truncate batch-timeout=60s
'
test_expect_success 'mark flushes an open group-commit batch' '
	$GC_RACE mark
'
test_expect_success 'sweep flushes an open group-commit batch' '
	$GC_RACE sweep
'
test_expect_success 'restore default content-sqlite config' '
	flux module reload content-sqlite truncate
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
	flux content load --bypass-cache $(cat gcroot.ref) >gcroot.out &&
	test "$(cat gcroot.out)" = "{\"ver\":1,\"type\":\"dir\",\"data\":{}}"
'

test_expect_success 'remove content-sqlite and content modules' '
	flux module remove content-sqlite &&
	flux module remove content
'

test_done
