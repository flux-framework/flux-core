#!/bin/sh

test_description='Test that flux-gc preserves the empty-directory blob'

. `dirname $0`/sharness.sh

# See also: flux-framework/flux-core#7808

test_under_flux 1 kvs --conf=content-sqlite.max_checkpoints=2

# Derive the empty-dir blobref for this instance's content hash: it is the root
# of a freshly created (empty) namespace.  Creating and removing the probe
# namespace does not store the blob or leave a lasting reference to it.
test_expect_success 'derive the empty-directory blobref' '
	flux kvs namespace create emptydir-probe &&
	flux kvs getroot -N emptydir-probe -b >emptydir.ref &&
	flux kvs namespace remove emptydir-probe &&
	test -s emptydir.ref
'

# Commit only non-empty content (no empty directories), then sync twice so the
# epoch advances past the empty-dir blob (stored at epoch 0) and the initial
# empty root ages off the two retained checkpoints.  The empty-dir blob is now
# unreferenced and below the gc horizon: without the fix it is reclaimable.
test_expect_success 'commit non-empty content and advance the epoch' '
	flux kvs put a.b.c=1 &&
	flux kvs sync &&
	flux kvs put a.b.d=2 &&
	flux kvs sync &&
	flux content flush
'

# Without the fix gc reclaims the empty-dir blob here and the content load
# (from # the backing store, past both caches) fails.  With the fix gc
# marks the empty-dir blob and it survives.
test_expect_success 'flux gc preserves the unreferenced empty-directory blob' '
	flux gc -v 2>&1 &&
	flux content dropcache &&
	flux content load $(cat emptydir.ref) >/dev/null
'

# End to end: a fresh namespace must still be creatable and writable after gc.
test_expect_success 'namespace create+commit works after gc' '
	flux kvs namespace create emptydir-after &&
	flux kvs put -N emptydir-after x.y=1 &&
	test "$(flux kvs get -N emptydir-after x.y)" = "1" &&
	flux kvs namespace remove emptydir-after
'

test_done
