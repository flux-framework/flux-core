#!/bin/sh
#
# ci=asan

test_description='Test flux-gc online garbage collection'

. `dirname $0`/content/content-helper.sh

. `dirname $0`/sharness.sh

SIZE=1
export FLUX_CONF_DIR=$(pwd)
test_under_flux ${SIZE} full --conf=content-sqlite.max_checkpoints=2

test_expect_success 'create some KVS content' '
	flux kvs put test.a=1 test.b=2 test.c=3 &&
	flux kvs put garbage.x=100 garbage.y=200
'

test_expect_success 'remove garbage keys to create unreferenced blobs' '
	flux kvs unlink garbage.x garbage.y
'

test_expect_success 'flux gc runs successfully' '
	flux gc -v > gc.out 2>&1 &&
	grep "gc complete" gc.out
'

test_expect_success 'flux gc reports marked and reclaimed counts' '
	grep "gc complete: marked .* reclaimed .* blobs" gc.out
'

test_expect_success 'flux fsck verifies all blobs referenced after GC' '
	flux fsck
'

test_expect_success 'verify KVS data still accessible after GC' '
	test "$(flux kvs get test.a)" = "1" &&
	test "$(flux kvs get test.b)" = "2" &&
	test "$(flux kvs get test.c)" = "3"
'

test_expect_success 'verify garbage keys are gone' '
	test_must_fail flux kvs get garbage.x
'

# Overwriting a key repeatedly orphans the old value blobs.  Sync after the
# churn so a checkpoint advances the epoch past them (making them epoch < H),
# then confirm GC actually reclaims them by watching object_count fall.
test_expect_success 'GC reclaims blobs orphaned by overwrite (object_count drops)' '
	for i in $(seq 1 50); do
		flux kvs put churn.key="value-$i" || return 1
	done &&
	flux kvs sync &&
	before=$(flux module stats content-sqlite | jq .object_count) &&
	flux gc -v >gc_churn.out 2>&1 &&
	grep "gc complete" gc_churn.out &&
	after=$(flux module stats content-sqlite | jq .object_count) &&
	test ${after} -lt ${before} &&
	test "$(flux kvs get churn.key)" = "value-50" &&
	flux fsck
'

test_expect_success 'test GC with --no-cache option' '
	flux gc -v --no-cache > gc_nocache.out 2>&1 &&
	grep "gc complete" gc_nocache.out
'

test_expect_success 'GC with --maxreqs raises the mark-phase window' '
	flux gc -v --maxreqs=64 > gc_maxreqs.out 2>&1 &&
	grep "gc complete" gc_maxreqs.out
'

test_expect_success 'GC rejects an invalid --maxreqs value' '
	test_must_fail flux gc --maxreqs=0 &&
	test_must_fail flux gc --maxreqs=-1
'

test_expect_success 'create private namespace and verify GC preserves it' '
	flux kvs namespace create test-ns &&
	flux kvs put --namespace=test-ns private.data=secret &&
	flux gc -vv > gc_ns.out 2>&1 &&
	grep "enumerated.*private namespace roots" gc_ns.out &&
	test "$(flux kvs get --namespace=test-ns private.data)" = "secret"
'

test_expect_success 'flux fsck verifies integrity after private namespace GC' '
	flux fsck
'

test_expect_success 'verify namespace data survives GC' '
	test "$(flux kvs get --namespace=test-ns private.data)" = "secret"
'

test_expect_success 'GC preserves realistic job data in private namespace' '
	flux kvs namespace create job-test-ns &&
	flux kvs put --namespace=job-test-ns guest.exec.eventlog="init" &&
	flux kvs put --namespace=job-test-ns guest.exec.eventlog="starting" &&
	flux kvs put --namespace=job-test-ns guest.output="job output line 1" &&
	flux kvs put --namespace=job-test-ns guest.output="job output line 2" &&
	flux kvs put --namespace=job-test-ns guest.R="{\"version\":1,\"resources\":[]}" &&
	flux kvs mkdir --namespace=job-test-ns guest.shell.0 &&
	flux kvs put --namespace=job-test-ns guest.shell.0.info="shell info" &&
	flux kvs getroot --namespace=job-test-ns -b > job_ns_root.ref &&
	flux gc -vv > gc_job_ns.out 2>&1 &&
	grep "enumerated.*private namespace roots" gc_job_ns.out &&
	test "$(flux kvs get --namespace=job-test-ns guest.exec.eventlog)" != "" &&
	test "$(flux kvs get --namespace=job-test-ns guest.output)" != "" &&
	test "$(flux kvs get --namespace=job-test-ns guest.R)" != "" &&
	test "$(flux kvs get --namespace=job-test-ns guest.shell.0.info)" = "shell info" &&
	flux fsck &&
	flux fsck --rootref=$(cat job_ns_root.ref) &&
	flux kvs namespace remove job-test-ns
'

test_expect_success 'GC preserves multiple private namespaces' '
	for i in $(seq 1 5); do
		flux kvs namespace create job-ns-$i &&
		flux kvs put --namespace=job-ns-$i guest.data="job-$i-data" &&
		flux kvs put --namespace=job-ns-$i guest.eventlog="job-$i-running" &&
		flux kvs getroot --namespace=job-ns-$i -b > job_ns_${i}_root.ref || return 1
	done &&
	flux gc -vv > gc_multi_ns.out 2>&1 &&
	for i in $(seq 1 5); do
		test "$(flux kvs get --namespace=job-ns-$i guest.data)" = "job-$i-data" &&
		test "$(flux kvs get --namespace=job-ns-$i guest.eventlog)" = "job-$i-running" || return 1
	done &&
	flux fsck &&
	for i in $(seq 1 5); do
		flux fsck --rootref=$(cat job_ns_${i}_root.ref) || return 1
	done &&
	for i in $(seq 1 5); do
		flux kvs namespace remove job-ns-$i || return 1
	done
'

test_expect_success 'data committed during GC is preserved' '
	flux kvs put before.a=1 before.b=2 &&
	flux kvs sync &&
	flux gc -v > gc_concurrent.out 2>&1 &
	gc_pid=$! &&
	for i in $(seq 1 10); do
		flux kvs put during.$i=$i || return 1
	done &&
	wait $gc_pid &&
	for i in $(seq 1 10); do
		test "$(flux kvs get during.$i)" = "$i" || return 1
	done &&
	flux fsck
'

test_expect_success 'private namespace commits during GC are preserved' '
	flux kvs namespace create job-concurrent-ns &&
	flux kvs put --namespace=job-concurrent-ns guest.initial="data" &&
	{ flux gc -vv > gc_ns_concurrent.out 2>&1 & } &&
	gc_pid=$! &&
	for i in $(seq 1 10); do
		flux kvs put --namespace=job-concurrent-ns guest.log.$i="entry-$i" || return 1
	done &&
	wait $gc_pid &&
	flux kvs getroot --namespace=job-concurrent-ns -b > job_concurrent_ns_root.ref &&
	test "$(flux kvs get --namespace=job-concurrent-ns guest.initial)" = "data" &&
	for i in $(seq 1 10); do
		test "$(flux kvs get --namespace=job-concurrent-ns guest.log.$i)" = "entry-$i" || return 1
	done &&
	flux fsck &&
	flux fsck --rootref=$(cat job_concurrent_ns_root.ref) &&
	flux kvs namespace remove job-concurrent-ns
'

test_expect_success 're-referencing old garbage blob during GC works' '
	flux kvs put dedup.original="REUSED_CONTENT_STRING" &&
	flux kvs sync &&
	flux kvs unlink dedup.original &&
	flux kvs sync &&
	flux gc -v > gc_dedup.out 2>&1 &
	gc_pid=$! &&
	flux kvs put dedup.reused="REUSED_CONTENT_STRING" &&
	wait $gc_pid &&
	test "$(flux kvs get dedup.reused)" = "REUSED_CONTENT_STRING" &&
	flux fsck
'

test_expect_success 'large appended values committed during GC survive' '
	flux kvs put large.val="initial" &&
	flux kvs sync &&
	flux gc -v > gc_append.out 2>&1 &
	gc_pid=$! &&
	for i in $(seq 1 20); do
		flux kvs put --append large.val="append-$i-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" || return 1
	done &&
	wait $gc_pid &&
	result=$(flux kvs get large.val) &&
	echo "$result" | grep -q "initial" &&
	echo "$result" | grep -q "append-20" &&
	flux fsck
'

test_expect_success 'GC preserves older retained checkpoints' '
	flux kvs put checkpoint.v1="version1" &&
	flux kvs sync &&
	flux kvs getroot -b > checkpoint_v1.ref &&
	flux kvs put checkpoint.v2="version2" &&
	flux kvs sync &&
	flux kvs getroot -b > checkpoint_v2.ref &&
	flux gc -v > gc_checkpoints.out 2>&1 &&
	flux fsck &&
	flux fsck --rootref=$(cat checkpoint_v1.ref) &&
	flux fsck --rootref=$(cat checkpoint_v2.ref)
'

test_expect_success 'checkpoint retention boundary: oldest checkpoint is pruned' '
	flux kvs put boundary.v1="boundary1" &&
	flux kvs sync &&
	flux kvs getroot -b > boundary_v1.ref &&
	flux kvs put boundary.v2="boundary2" &&
	flux kvs sync &&
	flux kvs getroot -b > boundary_v2.ref &&
	flux kvs put boundary.v3="boundary3" &&
	flux kvs sync &&
	flux kvs getroot -b > boundary_v3.ref &&
	flux gc -v > gc_boundary.out 2>&1 &&
	flux fsck &&
	test_must_fail flux fsck --rootref=$(cat boundary_v1.ref) &&
	flux fsck --rootref=$(cat boundary_v2.ref) &&
	flux fsck --rootref=$(cat boundary_v3.ref)
'

test_expect_success 'remove private namespace' '
	flux kvs namespace remove test-ns
'

# The graft hazard: when a job completes, its private namespace is copied by
# reference into the primary tree (flux_kvs_copy / "flux kvs copy") and then
# destroyed.  The grafted subtree's blobs are re-referenced but never re-stored,
# so they keep their original (old) epoch.  In the window after the graft but
# before the next primary checkpoint captures it, those blobs are reachable ONLY
# from the live primary root -- not from any retained checkpoint, and not from a
# live private namespace root (the namespace is gone).  GC must mark the live
# primary root or it would sweep them, losing a completed job's data.
#
# Reproducing the window requires care:
# - The value must exceed BLOBREF_MAX_STRING_SIZE (72) so it is stored as a
#   separate "valref" blob, not inlined in the directory treeobj.  An inlined
#   value would be re-serialized (re-stored at the current epoch) by the graft
#   commit, masking the hazard; only a separately-stored blob is re-referenced
#   without re-storing and so keeps its old epoch.
# - "flux content flush" forces the namespace blobs from the content cache out
#   to the backing store.  GC sweeps the backing store, so blobs that never
#   reached it cannot demonstrate the hazard.
# - The syncs advance the epoch past the namespace blobs (epoch < H) while NOT
#   checkpointing the graft (no sync after the copy), so the grafted subtree is
#   reachable only from the live primary root.
# - Survival must be checked against the BACKING STORE, not "flux kvs get": the
#   swept blobs remain in the KVS module's in-memory cache, which a plain get is
#   served from.  "flux content dropcache" then "flux content load <blobref>"
#   bypasses that cache and faults from the backing store -- failing iff swept.
test_expect_success 'graft hazard: GC preserves a grafted, uncheckpointed namespace' '
	bigval="graft-survivor-$(printf "x%.0s" $(seq 1 80))" &&
	flux kvs namespace create graft-ns &&
	flux kvs put --namespace=graft-ns old="$bigval" &&
	flux content flush &&
	nsref=$(flux kvs getroot --namespace=graft-ns -b) &&
	valref=$(flux kvs get --namespace=graft-ns --treeobj old \
	    | sed "s/.*\"\(sha1-[0-9a-f]*\)\".*/\1/") &&
	flux kvs put primary.bump1=1 && flux kvs sync &&
	flux kvs put primary.bump2=2 && flux kvs sync &&
	flux kvs copy --src-namespace=graft-ns . job.graft.guest &&
	flux kvs namespace remove graft-ns &&
	flux gc -vv > gc_graft.out 2>&1 &&
	grep -i "enumerated live primary root" gc_graft.out &&
	flux content dropcache &&
	flux content load "$nsref" >/dev/null &&
	flux content load "$valref" >/dev/null &&
	flux fsck
'

# A private namespace can be destroyed (e.g. a job completing) in the window
# between GC's kvs.namespace-list and the per-namespace kvs.getroot.  A vanished
# namespace returns ENOENT, or ENOTSUP from the rank 0 kvs; GC must skip it and
# continue, not abort.  --test-delay-after-list widens that window so the test
# can remove the namespace deterministically inside it.
test_expect_success NO_CHAIN_LINT 'GC tolerates a namespace removed mid-enumeration' '
	flux kvs namespace create race-ns &&
	flux kvs put --namespace=race-ns data=value &&
	flux gc --test-delay-after-list=3 -v >gc_race.out 2>&1 &
	gc_pid=$! &&
	sleep 1 &&
	flux kvs namespace remove race-ns &&
	wait $gc_pid &&
	flux fsck
'

# GC is conservative: with nothing unreferenced, a cycle must reclaim nothing.
# Run a full GC first to clear any prior garbage, then a second GC should
# reclaim zero blobs -- proving GC does not over-sweep reachable, recent data.
test_expect_success 'GC is a no-op when there is no garbage' '
	flux kvs put noop.a=1 noop.b=2 &&
	flux kvs sync &&
	flux gc -v >gc_noop1.out 2>&1 &&
	flux gc -v >gc_noop2.out 2>&1 &&
	grep "gc complete: marked .* reclaimed 0 blobs" gc_noop2.out &&
	test "$(flux kvs get noop.a)" = "1"
'

# Crash safety: a run that stops between mark and sweep must leave no data at
# risk, and a subsequent run must re-mark from scratch and complete normally.
# --test-mark-only runs the (durable) mark phase then stops without sweeping,
# the same state a crash there would leave.  Confirm the data is intact, then
# run a full GC and verify via dropcache + content load that it still faults
# from the backing store and fsck passes.
test_expect_success 'crash safety: mark-only run leaves data intact, next GC completes' '
	bigval="crash-survivor-$(printf "x%.0s" $(seq 1 80))" &&
	flux kvs put crash.keep="$bigval" &&
	flux content flush &&
	keepref=$(flux kvs get --treeobj crash.keep \
	    | sed "s/.*\"\(sha1-[0-9a-f]*\)\".*/\1/") &&
	flux kvs put crash.garbage=junk &&
	flux kvs unlink crash.garbage &&
	flux kvs sync &&
	flux gc --test-mark-only -v >gc_markonly.out 2>&1 &&
	grep -i "stopping after mark" gc_markonly.out &&
	test "$(flux kvs get crash.keep)" = "$bigval" &&
	flux fsck &&
	flux gc -v >gc_resume.out 2>&1 &&
	flux content dropcache &&
	flux content load "$keepref" >/dev/null &&
	flux fsck
'

test_expect_success 'create content and GC in sequence' '
	for i in $(seq 1 5); do
		flux kvs put seq.$i=$i &&
		flux gc -v || return 1
	done &&
	test "$(flux kvs get seq.5)" = "5"
'

test_expect_success NO_CHAIN_LINT 'test multiple concurrent GC runs (should be safe)' '
	flux gc >gc1.out 2>&1 &
	flux gc >gc2.out 2>&1 &
	wait
'

test_expect_success NO_CHAIN_LINT 'flux fsck verifies integrity after concurrent GC' '
	flux fsck
'

test_expect_success NO_CHAIN_LINT 'verify KVS still works after concurrent GC' '
	flux kvs put final.test=done &&
	test "$(flux kvs get final.test)" = "done"
'

#
# Legacy database migration.
#
# A content.sqlite created before GC support has no epoch column.  On
# upgrade the column is added with every existing row defaulting to
# epoch 0.  The first GC must therefore mark reachable blobs (lifting
# them above 0) before it sweeps, or it would reclaim live legacy data.
# These tests run in their own persistent instances so the backing
# store can be mangled offline between restarts.
#

QUERYCMD="flux python ${FLUX_SOURCE_DIR}/t/scripts/sqlite-query.py"
WRITECMD="flux python ${FLUX_SOURCE_DIR}/t/scripts/sqlite-write.py"

test_expect_success 'create a persistent instance with KVS content' '
	statedir=$(mktemp -d --tmpdir=${TMPDIR:-/tmp}) &&
	flux start --setattr=statedir=${statedir} \
	    "flux kvs put --sync legacy.keep=keepval legacy.also=alsoval"
'
test_expect_success 'simulate a pre-GC database by dropping the epoch column' '
	$WRITECMD -t 100 ${statedir}/content.sqlite \
	    "CREATE TABLE objs (hash BLOB PRIMARY KEY, size INT, object BLOB)" &&
	$WRITECMD -t 100 ${statedir}/content.sqlite \
	    "INSERT INTO objs SELECT hash, size, object FROM objects" &&
	$WRITECMD -t 100 ${statedir}/content.sqlite \
	    "DROP TABLE objects" &&
	$WRITECMD -t 100 ${statedir}/content.sqlite \
	    "ALTER TABLE objs RENAME TO objects"
'
test_expect_success 'legacy database has no epoch column' '
	$QUERYCMD -t 100 ${statedir}/content.sqlite \
	    "PRAGMA table_info(objects)" >legacy_cols.out &&
	test_must_fail grep -w epoch legacy_cols.out
'
test_expect_success 'restart migrates the database and data is intact' '
	flux start --setattr=statedir=${statedir} \
	    "flux kvs get legacy.keep" >migrated.out &&
	grep keepval migrated.out
'
test_expect_success 'migration restored the epoch column' '
	$QUERYCMD -t 100 ${statedir}/content.sqlite \
	    "PRAGMA table_info(objects)" >migrated_cols.out &&
	grep -w epoch migrated_cols.out
'
test_expect_success 'legacy rows were migrated at epoch 0' '
	$QUERYCMD -t 100 --nokey ${statedir}/content.sqlite \
	    "SELECT COUNT(*) FROM objects WHERE epoch = 0" >zero.out &&
	test $(cat zero.out) -gt 0
'
test_expect_success 'GC after migration reclaims garbage and keeps reachable legacy data' '
	flux start --setattr=statedir=${statedir} \
	    "flux module stats content-sqlite | jq .current_epoch \
	         >${statedir}/before.epoch && \
	     flux module stats content-sqlite | jq .object_count \
	         >${statedir}/before.count && \
	     flux gc -v && \
	     flux module stats content-sqlite | jq .object_count \
	         >${statedir}/after.count && \
	     flux kvs get legacy.keep && \
	     flux kvs get legacy.also && \
	     flux fsck" >gc_legacy.out 2>&1 &&
	grep keepval gc_legacy.out &&
	grep alsoval gc_legacy.out &&
	# current_epoch is nonzero even though this store was just migrated at
	# epoch 0: each of the flux start/stop cycles above checkpoints the KVS on
	# shutdown, and every checkpoint advances the epoch.
	test $(cat ${statedir}/before.epoch) -gt 0 &&
	test $(cat ${statedir}/after.count) -lt $(cat ${statedir}/before.count)
'
# Downgrade compatibility: an older release writes objects/(hash,size,object)
# without the epoch column.  The migrated table must still accept such an
# insert (epoch defaulting to 0), so reverting to an old release is safe.
test_expect_success 'migrated objects table accepts an old-style insert' '
	$WRITECMD -t 100 ${statedir}/content.sqlite \
	    "INSERT INTO objects (hash, size, object) VALUES (424242, 3, 777)" &&
	$QUERYCMD -t 100 --nokey ${statedir}/content.sqlite \
	    "SELECT epoch FROM objects WHERE hash = 424242" >oldinsert.epoch &&
	test "$(cat oldinsert.epoch)" = "0"
'

# GC requires the mark/sweep/gc-info primitives, which only content-sqlite
# implements.  Against content-files (which has no gc-info handler) flux-gc
# must fail cleanly with a useful message, not crash or corrupt anything.
test_expect_success 'flux gc fails cleanly with the content-files backing store' '
	test_must_fail flux start \
	    -Scontent.backing-module=content-files \
	    -Sstatedir=$(mktemp -d) \
	    "flux kvs put cf.a=1 && flux kvs sync && flux gc" \
	    >gc_files.out 2>&1 &&
	grep -i "current epoch\|service method\|gc-info" gc_files.out
'

test_done
