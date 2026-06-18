.. _kvs_gc:

=======================================================
Online Garbage Collection for KVS Content Backing Store
=======================================================

Problem
=======

The Flux content service is append-only. As the KVS evolves through commits,
old versions of the hash tree accumulate in the backing store. Offline GC uses
:man1:`flux-dump` to dump the current KVS snapshot and :man1:`flux-restore` to
recreate the backing store from scratch. This requires downtime and doesn't
help long-running instances.

**Goal**: Reclaim unreferenced blobs while the instance is running.

**Safety requirement**: GC must never delete a referenced blob. It is
acceptable (and expected) that some garbage survives multiple GC cycles - we
are conservative by design.

Related Documents
=================

- :doc:`kvs`
- :doc:`rfc:spec_10`
- :doc:`rfc:spec_11`

Approach
========

GC runs as a standalone **external tool**, :man1:`flux-gc`, in the same spirit
as :man1:`flux-dump` / :man1:`flux-restore`: a manual, instance-owner operation
that walks the KVS tree and interacts with the content store over RPC. The
tool is a stateless orchestrator; the backing store provides a few small,
content-agnostic primitives; and a monotonic **epoch** stamped on every stored
blob acts as the reclamation horizon that makes the whole thing safe against
a live, committing instance.

The horizon
-----------

The single idea the rest of the design rests on: at the start of a run the tool
reads the current epoch and **freezes** it as the horizon :math:`H`. It then
marks every blob reachable from a marked root up to :math:`H`, and sweeps only
blobs whose epoch is :math:`< H`. Because the epoch only ever increases and
every store stamps the *current* epoch (:math:`\geq H`), any blob the tool
didn't mark is guaranteed to be either genuine garbage (epoch :math:`< H`) or
too recent to touch (epoch :math:`\geq H`). The horizon is what lets the tool
take its time, crash, or even run concurrently with another copy of itself
without endangering a referenced blob. :math:`H`, the mark phase, and the sweep
phase are developed in full below; keep this horizon in mind as the key
invariant.

This division of labor is deliberate:

- **The tool understands RFC 11.** Like :man1:`flux-dump`, it walks the tree by
  loading treeobjs and parsing them. It knows *structurally* whether a child
  blobref is a raw-data leaf (a ``valref``'s ``data[]``) or a treeobj (a
  ``dirref``/``dir`` entry), so it never loads raw-data blobs and never has to
  guess a blob's type. It shares the walker (``kvs_treewalk``) with
  :man1:`flux-dump` and :man1:`flux-fsck`, which keeps a bounded window of
  content loads in flight to hide the per-node round-trip latency that
  otherwise dominates a walk of a large KVS. GC prunes subtrees it has already
  walked this run (roots overlap heavily) and skips loading ``valref`` leaves
  entirely, since marking needs only their blobrefs.
- **The backing store stays content-agnostic.** content-sqlite gains only an
  epoch column and three primitives (mark / sweep / gc-info); it never parses
  treeobjs.
- **The epoch provides concurrency safety**, not the tool. The tool can run for
  minutes, crash, or be killed at any point — or be started a second time while
  a first run is still going — without risking a referenced blob, because
  correctness rests on server-side atomic operations and the horizon, not on
  the tool holding any lock or consistent snapshot.

Background: how content and the KVS fit together
=================================================

A few facts about the pre-GC KVS/content implementation are important to
this design:

- **content-sqlite is a content-agnostic blob store.** It maps a hash to an
  opaque blob (``objects(hash, size, object)``) and knows nothing about
  treeobjs. Requests to store duplicate blobs are no-ops.

- **The KVS is the only component that understands the tree** and which *roots*
  are currently live. It maintains all roots (primary and private)
  and can iterate them.

- **Only the primary namespace is checkpointed.** ``checkpoint_put()`` in
  ``kvs.c`` checkpoints ``KVS_PRIMARY_NAMESPACE`` only. Private (per-job,
  guest-owned) namespaces are in-memory roots whose treeobjs and values live in
  the *same* shared content backing store but appear in **no** checkpoint. This
  omission is the central correctness hazard and is addressed below.

- **The checkpoint value is opaque JSON** stored verbatim by content-sqlite in
  the ``checkpt_v2`` table, which has a dense
  ``id INTEGER PRIMARY KEY AUTOINCREMENT``. We use that ``id`` as the epoch.

- **Re-referenced blobs would not otherwise re-reach the backing store.** The
  content cache does not re-flush a blob that is already cached clean, so
  without intervention a blob re-referenced by a new commit never updates its
  epoch. The dedup-dirty fix marks such an entry dirty on re-store so it is
  re-flushed to content-sqlite (without rewriting the object) — a hard
  prerequisite for epoch refresh.

- **content-sqlite writes are atomic and serialized per blob.** It runs a
  single reactor thread, and SQLite gives statement-level atomicity with
  single-writer semantics, so row operations on a given blob never partially
  apply or interleave.

The epoch
=========

**Epoch** is a dense, monotonic counter maintained by content-sqlite: the
``checkpt_v2.id`` autoincrement value. Each checkpoint advances the epoch by
exactly one, with no gaps (SQLite ``AUTOINCREMENT`` never reuses ids, even
across prune). ``current_epoch = MAX(id)`` of ``checkpt_v2``, seeded at module
open and advanced in the ``checkpoint-put`` handler.

.. note::

   Using the checkpoint ``id`` (not the KVS commit ``sequence``, which jumps
   per-commit) keeps epochs dense and 64-bit, avoiding sparse gaps and 32-bit
   wraparound, and means content-sqlite assigns epochs itself without parsing
   the checkpoint value.

**Every stored blob is stamped with** ``current_epoch``:

.. code-block:: sql

   INSERT INTO objects (hash, size, object, epoch)
   VALUES (?, ?, ?, ?)            -- current_epoch
   ON CONFLICT(hash) DO UPDATE SET epoch = excluded.epoch;   -- object NOT rewritten

Combined with the dedup-dirty fix, a blob re-referenced by a new commit has its
epoch refreshed to ``current_epoch``. A blob's epoch thus records **when it was
last touched** — stored, re-stored via dedup, or marked reachable by GC.

Schema change
=============

.. code-block:: sql

   ALTER TABLE objects ADD COLUMN epoch INT DEFAULT 0;   -- when missing

Added to existing databases at open (detected via ``PRAGMA table_info``).
Pre-existing rows get ``epoch = 0``; the first GC's mark phase refreshes every
reachable blob before any sweep (see ordering invariant), so legacy blobs that
are reachable survive and the rest are collected. No secondary indexes are
required.

Enumerating private namespace roots
====================================

The mark phase is only safe if it marks from **every root the KVS could still
serve a read from**. Only the primary namespace is checkpointed; private
(per-job) namespaces are live roots in ``kvsroot_mgr`` whose blobs share the
backing store. A private namespace holds the user-writable portion of a job's
KVS (not system-owned data such as jobspec, R, or the primary eventlog, which
live in the primary namespace under the job's directory). Any private-namespace
data the job wrote once and has not since modified is stored once and never
re-stored, so if GC marked only from checkpoints it would age out and be swept
while the namespace is still alive — silent data loss.

**The tool snapshots the current private namespace roots directly from the KVS
at mark time** (``kvs.namespace-list`` to enumerate them, ``kvs.getroot`` for
each rootref), since they appear in no checkpoint.

GC therefore marks from the union of:

- **Stored checkpoints** — all retained primary checkpoints, from
  ``content-backing.checkpoint-get``, which protect everything reachable from
  recent primary roots (and support rollback/recovery to them).
- **The live primary root** — read with ``kvs.getroot`` *after* the
  private-namespace snapshot. This protects data re-referenced into the primary
  tree but not yet checkpointed; see "The graft hazard" below for why this is
  required and why the ordering matters.
- **Current private namespace roots** — a point-in-time snapshot from the KVS.

Marking the live primary root also ensures any primary data not yet
checkpointed is marked directly, rather than relying on epoch recency to
protect it — which is strictly safer.

This deliberately keeps the **checkpoint format unchanged** (primary only).
Persisting private namespaces across a restart is a separate concern owned by
the preserve-running-jobs effort (see "Relationship to preserving running
jobs"); GC reads roots from the running KVS and is agnostic to how they are
persisted.

Coverage:

- **Completed jobs** have had their namespace root grafted into the primary
  tree. Once a checkpoint captures the graft they are reachable from a
  checkpoint root; in the window before that checkpoint they are reachable only
  from the live primary root, which is why GC marks it (see "The graft
  hazard").
- **Running jobs** have a live private namespace root returned by the live-root
  query. A namespace created mid-cycle is covered by epoch recency (its blobs
  are stamped :math:`\geq H`) until the next cycle picks up its root.
- **The KVS symlink that points at a running job's namespace is not followed.**
  It is a target string with no blobref, so a tree walk does not (and must not)
  traverse into the namespace through it. This is exactly why GC enumerates
  roots explicitly instead of relying on tree reachability.

If GC is ever run with the KVS module not loaded, there are no live private
namespaces, so marking from the latest checkpoint alone is correct — mirroring
:option:`flux dump --checkpoint`.

The graft hazard
----------------

When a job completes, job-exec grafts its private namespace into the primary
tree with :man3:`flux_kvs_copy` — copying the namespace root into the job's
guest directory in the primary namespace — and then removes the namespace.
This creates a content-addressed *snapshot*: it reads the namespace root
treeobj and re-puts it at the destination with
:man3:`flux_kvs_txn_put_treeobj`, so the primary commit installs a single
dirref pointing at the existing namespace subtree. **The subtree blobs are
re-referenced but never re-stored**, so they retain their original epoch.
Neither epoch-refresh protection helps:

- the dedup-dirty / epoch-refresh path only fires for a blob whose *content* is
  presented to ``store_cache`` — the subtree a grafted dirref points at is
  never presented; and
- the :math:`\geq H` recency rule only covers blobs *stored* since the horizon
  — the grafted blobs were stored long ago, under the namespace.

This opens a window: after the graft commits and the namespace is removed, but
before the next primary checkpoint captures the graft, the subtree is reachable
from neither a checkpoint root nor a live private namespace root. If its blobs
predate the latest checkpoint — the common case for any job whose data is older
than one checkpoint interval — they have :math:`\text{epoch} < H` and would be
swept: silent loss of a completed job's KVS data. **Marking the live primary
root closes the window**, since once the namespace is gone the graft is always
present in the live primary root.

**Ordering invariant:** the live primary root must be read *after* the
private-namespace snapshot. To see why, name the four moments involved:

- :math:`N` — when the tool snapshots the private namespace roots.
- :math:`P` — when the tool reads the live primary root.
- :math:`c` — when a completing job grafts its namespace into the primary tree.
- :math:`d` — when that job then removes its namespace, necessarily after the
  graft, so :math:`c < d`.

A job's grafted data slips through only if it is missing from *both* root sets:

- absent from the private snapshot, i.e. the namespace was already gone when we
  took it: :math:`d \leq N`; **and**
- absent from the primary root we read, i.e. the graft had not yet happened
  when we read it: :math:`c \geq P`.

Chaining those with :math:`c < d` gives :math:`P \leq c < d \leq N`, which
requires :math:`P < N` — the primary root read *before* the private snapshot.
Reading the primary root last guarantees the opposite, :math:`N \leq P`, so the
gap cannot occur: every graft is caught by at least one of the two root sets.

The same protection generalizes to any re-reference-without-restore into the
primary, such as an operator's own :option:`flux kvs copy`.

Relationship to preserving running jobs
----------------------------------------

Today private namespaces are created and destroyed by job-exec per job; on
destroy, the namespace root is grafted into the primary tree, and while a job
runs a primary-tree symlink points at its private namespace. Running jobs are
not yet preserved across a restart, but will need to be.

GC is intentionally decoupled from that effort. GC's only requirement is that
**a running job's namespace root is enumerable as a live root whenever the
instance considers that job active.** It reads roots from the running KVS at
mark time and does not care how — or whether — they are persisted. However the
preserve-running-jobs design eventually reconstitutes running namespaces after
a restart (re-checkpointing them, persisting per-job state, re-creating them
from job-exec, etc.), they will reappear as live roots in ``kvsroot_mgr`` and
GC will mark them with no change to this plan. This is why the checkpoint
format is deliberately left untouched here: persisting private namespaces is
that effort's decision to make, not GC's.

One existing code path to revisit under that effort is ``checkpoint_running()``
in ``job-exec/checkpoint.c``, which records running jobs' rootrefs as an opaque
value in ``job-exec.kvs-namespaces`` that GC does not traverse, so
reconstituting a namespace from it must re-establish the live root before the
namespace stops being enumerable.

Backing store primitives
=========================

content-sqlite gains three RPCs, all content-agnostic and (like other backing
ops) rank-0-local / instance-owner only:

- **content-backing.mark** — given a batch of blobrefs and target epoch
  :math:`H`, set :math:`\text{epoch} = \max(\text{epoch}, H)`. Idempotent and
  monotonic.
- **content-backing.sweep** — ``DELETE ... WHERE epoch < H LIMIT batch``,
  returning rows deleted and rows remaining. A server-side conditional delete,
  so the tool never enumerates the store.
- **content-backing.gc-info** — return :math:`\text{current_epoch}` and
  :math:`\text{COUNT}(\text{epoch} < H)` (for ``--dry-run``).

The tool's algorithm
====================

.. code-block:: python

   H = gc-info.current_epoch                       # freeze the horizon at cycle start

   roots = []
   for cp in content-backing.checkpoint-get():     # stored primary checkpoints
       roots += [cp.rootref]
   for ns in kvs.namespace-list() if ns.private:   # private namespace snapshot
       roots += [kvs.getroot(ns).rootref]
   roots += [kvs.getroot(primary).rootref]         # live primary root, read LAST

   # MARK: walk like flux-dump, but refresh epochs instead of emitting values
   visited = set()                                 # treeobj blobrefs walked this run
   for root in roots:
       walk(root):
           if blobref in visited: skip             # roots share large subtrees
           visited.add(blobref)
           mark([blobref], H)                      # batched mark RPCs
           if treeobj (dirref/dir/valref):
               load + parse
               valref.data[]  -> mark(leaves, H)   # leaves never loaded
               dir/dirref     -> recurse

   # SWEEP: only after mark fully completes (ordering invariant)
   while sweep(threshold=H, batch=N).remaining > 0:
       pass

**Ordering invariant:** sweep for a cycle must not begin until that cycle's
mark has fully completed. The tool always re-marks from scratch before
sweeping, so a crashed previous run is harmless.

Why it is safe against a live instance
=======================================

Two protections cover every referenced blob, both enforced by **atomic
server-side operations** — the store-upsert and the conditional sweep-delete
each apply atomically and are serialized per blob, so they cannot interleave to
leave a blob missing. This rests on SQLite's single-writer atomicity, not on
content-sqlite being single-threaded:

1. **Reachable from a marked root** (a stored checkpoint, the live primary
   root, or a private namespace snapshot root) → the tool marks it to *H*.
2. **Recently stored / re-referenced** → content-sqlite stamped it
   :math:`\text{epoch} \geq H`` at store time.

Sweep removes only :math:`\text{epoch} < H`. The races:

- **current_epoch only increases** (it is the checkpoint id), so every store
  during the run stamps :math:`\geq H`; new commits are never swept.
- **Uncheckpointed live data:** blobs committed since the latest checkpoint
  were stamped at :math:`\text{current_epoch} = H`, which :math:`< H` excludes;
  blobs committed in the gap before the latest checkpoint are reachable from
  its root (in the window) and get marked.
- **Dedup re-reference (the dangerous race):** a commit that re-references an
  old garbage blob :math:`B` forces a re-store (dedup-dirty fix), stamping
  :math:`B.\text{epoch} \geq H`. Even if a sweep batch deletes :math:`B` first,
  the re-store re-inserts it, and the dirty cache entry keeps readers from
  faulting an absent blob in the meantime. At quiescence :math:`B` exists.

The only blobs with :math:`\text{epoch} < H` are those last touched strictly
before the latest checkpoint **and** unreachable from any marked root — genuine
garbage. The horizon :math:`H` is what lets the tool take its time: marking may
run for minutes while commits and new checkpoints proceed, because nothing it
protects can drop below :math:`H`.

:math:`H` (the newest checkpoint epoch) is a policy choice, not a correctness
boundary: it is the most aggressive sweep that keeps every retained checkpoint
valid. Any lower threshold is equally safe and simply reclaims less, leaving a
recency margin of uncollected garbage. We default to :math:`H` because, having
marked all retained checkpoints and private namespace roots, everything below
:math:`H` is provably dead.

Why one horizon instead of per-checkpoint epochs
================================================

The tool marks every reachable blob to :math:`H` and sweeps everything below
:math:`H` — a single frozen value serving as both the mark target and the sweep
threshold.

A finer-grained alternative is possible and was considered. Let :math:`e_i`
denote the epoch of retained checkpoint :math:`i` (its ``checkpt_v2.id``), and
let :math:`e_{\text{old}}` denote the smallest such epoch — the oldest
*retained* checkpoint. The variant marks each blob with the epoch of the
*newest checkpoint that references it* (still a monotonic :math:`\max`, so
epochs only ever rise) and sweeps below :math:`e_{\text{old}}` rather than
:math:`H`.

That variant is **equally safe** — the live-instance horizon argument holds
with :math:`e_{\text{old}}` frozen in place of :math:`H`, because every store
stamps :math:`\text{current_epoch} \geq H \geq e_{\text{old}}` and so is never
swept — and it is arguably the more intuitive model, with real attractions:

- **Reclamation tracks checkpoint retention exactly.** With a fixed retention
  window, a blob referenced only by the oldest checkpoint becomes eligible the
  moment that checkpoint is pruned (which raises :math:`e_{\text{old}}`) and
  nothing newer references it.
- **Fewer epoch writes.** Under the single horizon, :math:`H` rises every
  cycle, so every reachable blob takes an ``UPDATE epoch`` every cycle. With
  per-checkpoint epochs, a stable blob already at :math:`\geq e_i` makes its
  mark a no-op, so unchanged data is not rewritten.
- **It is the natural key for pruning the mark walk.** Because blobs are
  content-addressed, a treeobj already at :math:`\geq e_i` has an unchanged,
  already-marked subtree, so the walk can stop descending — the *cached subtree
  traversal* optimization (see Future work). A single global :math:`H` cannot
  express which checkpoint a blob belongs to, and so cannot drive this.

We nonetheless use the single horizon :math:`H` for this implementation:

- It is **one frozen value, not two**, which is simpler to reason about and to
  get right.
- It is **more aggressive**: it reclaims unreachable garbage in
  :math:`[e_{\text{old}}, H)` immediately rather than carrying it for
  additional cycles.
- The mark **walk**, not the epoch writes, dominates GC cost until cached
  subtree traversal exists, so the write savings alone do not yet justify the
  extra moving part.

Per-checkpoint epochs become worthwhile precisely when cached subtree traversal
is implemented; the two belong together and are deferred together.

Crash safety
============

The tool holds no persistent state:

- ``mark`` is an idempotent epoch bump; a crashed partial mark just means the
  next run re-marks from scratch.
- ``sweep`` only ever removes blobs older than the window; a partial sweep left
  garbage, never live data.
- Kill ``-9`` at any point → no corruption, nothing to clean up. The
  :man1:`flux-dump` / :man1:`flux-restore` safety model.

Concurrent runs
===============

Two :man1:`flux-gc` processes running at once (e.g. cron launches a second
while a first is wedged) is **safe, by the same horizon argument** — no locking
is
required for correctness. Call the two runs :math:`A` and :math:`B`. Each run
:math:`R` has its own horizon :math:`H_R` and, as in "The graft hazard", its
own private-snapshot time :math:`N_R` and primary-root read time :math:`P_R`
(with :math:`N_R \leq P_R`, since every run reads the primary root last).

Concurrent safety is a corollary of a single per-run invariant:

  **A run's sweep deletes only blobs that are unreachable from every root the
  KVS could serve a read from** — every retained checkpoint, the live primary
  root, and every live private namespace root — at any time from the run's
  enumeration onward.

This is exactly what the root set plus the ordering invariant establish for one
run (see "The graft hazard" and "Why it is safe against a live instance"): any
blob run :math:`X` sweeps has :math:`\text{epoch} < H_X` *and* was not marked
by :math:`X`, which together imply no live root reaches it — and none can come
to reach it without first lifting its epoch to :math:`\geq H_X` (a store, a
dedup re-store, or a mark). Run :math:`B`'s roots are live roots, so they are
precisely the things run :math:`A`'s sweep is guaranteed not to touch.
Symmetrically for :math:`B`. Two further facts close the concurrent case:

- **Marks don't conflict.** ``mark`` sets
  :math:`\text{epoch} = \max(\text{epoch}, H)`, so overlapping marks from two
  runs only ever push an epoch up; neither can lower what the other relies on.
  A ``mark`` RPC racing the other run's ``sweep`` is harmless: each is an
  atomic SQL operation serialized per blob by SQLite.
- **The dangerous case is the graft, and it is already covered.** Consider the
  one way an *old* blob :math:`X` (:math:`\text{epoch} < H_A`) could become
  reachable from :math:`B`'s roots without being re-stored: a graft — a
  completed job's private namespace subtree re-referenced into the primary tree
  by a commit newer than :math:`H_A` that does **not** stamp :math:`X`. The
  question is only whether run :math:`A` might sweep :math:`X` out from under
  :math:`B`. It cannot, because :math:`A` itself already marked :math:`X`. Let
  :math:`c` be the time of the graft and :math:`d > c` the time the job then
  removes its namespace, and split on when the graft happened relative to
  :math:`A`'s reads:

  - **Graft at or before** :math:`A`'s primary-root read (:math:`c \leq P_A`):
    then :math:`A`'s primary root already contains the graft, so :math:`A`
    marked :math:`X` through it.
  - **Graft after** :math:`A`'s primary-root read (:math:`c > P_A`): the
    namespace is not removed until :math:`d > c > P_A \geq N_A`, so it was
    still live when :math:`A` took its private snapshot at :math:`N_A`, and
    :math:`A` marked :math:`X` through the namespace. (A namespace created
    *after* :math:`N_A` holds only blobs stored after :math:`H_A`, hence
    :math:`\geq H_A`, so there is no old :math:`X` to lose.)

  Either way :math:`X.\text{epoch} \geq H_A` before :math:`A` sweeps.

So concurrency is a *waste* (two full traversals, extra load on the store), not
a *hazard*. If avoiding that redundant work ever matters in practice it can be
handled by simple operational means; it is not a correctness concern and this
design prescribes no mechanism for it.

Safety invariants
=================

1. **No false deletions.** Marking from all stored checkpoints, the live
   primary root, and all current private namespace roots, plus epoch-recency
   for recently touched blobs, guarantees referenced blobs are never swept. The
   mark-before-sweep ordering guarantees legacy/epoch-0 blobs are refreshed
   before any delete.
2. **Eventual reclamation.** Genuine garbage ages out of the window and is
   collected, possibly after several cycles. Conservative by design.
3. **Concurrent correctness.** Commits proceed during GC: new stores get
   ``current_epoch ≥ H``; dedup re-stores refresh the epoch; atomic per-blob
   store/sweep operations (serialized by SQLite) prevent interleaving.
4. **Crash safety.** The tool is stateless and restartable; epochs persist; a
   killed run leaves no corruption.

Performance characteristics
============================

Per GC cycle:

- Mark: O(distinct treeobjs loaded + reachable leaves marked). Treeobjs are
  loaded and parsed by the tool; raw-data blobs are marked without loading.

  A per-run **visited set** of already-walked treeobj blobrefs keeps the
  *distinct* qualifier honest. The check sits *before* the ``content.load``, so
  the first time the root blobref of a subtree is seen the whole subtree below
  it is walked, and every later encounter of that same blobref returns
  immediately — pruning the entire subtree, and its loads, not just one node.
  Because blobs are content-addressed this dedup is exact and free: an
  unchanged subtree has the same root blobref everywhere it appears, so one hit
  prunes it.

  This matters because the marked roots overlap heavily. Consecutive retained
  checkpoints differ only along recently-modified paths, the live primary root
  differs from the newest checkpoint only by commits since that checkpoint, and
  a completed job's data, once grafted into the primary tree, is referenced by
  an identical blobref from the live primary root and from every checkpoint
  taken since the graft. So the large, immutable mass of weeks-old job data is
  loaded **once** rather than once per root, and adding the live primary root
  to the mark set (see "The graft hazard") costs only the delta since the last
  checkpoint — the rest is already in the visited set. Memory is bounded by the
  number of distinct interior treeobjs in the live tree (walked once), not by
  the number of roots. The part that does not dedup is running-job private
  namespaces, which are disjoint subtrees until graft — the small active slice,
  not the bulk.
- Sweep: O(total blobs) server-side scan of :math:`\text{epoch} < H`, batched.
- Space overhead: ~4 bytes/blob (one nullable INT epoch column).

Known Limitations
=================

**Stale roots on compute nodes.** Ranks > 0 cache their current KVS root and
update it via the eventually-consistent ``kvs.setroot`` event mechanism. A
compute node that is slow, partitioned, or stuck (e.g., in a memory reclaim
loop) may hold a stale root for an extended period. If enough new checkpoints
accumulate and GC prunes checkpoints beyond the retention window (default 5),
blobs referenced only by that stale root may be swept. When the compute node
recovers and attempts a lookup, it will fail with "blob not found."

Future work
===========

- **Cached subtree traversal.** Because blobs are content-addressed, a treeobj
  with an unchanged hash has an unchanged, still-reachable subtree. *Within* a
  run GC already prunes re-walks of subtrees shared between roots with a
  visited set; the future work is persisting that knowledge *across* runs by
  caching parent→child edges (keyed by the dense epoch), so stable branches can
  be bulk-marked without reloading — a large speedup for long-running instances
  with accumulated, immutable job data. Add only if the mark phase proves too
  slow. This pairs naturally with per-checkpoint mark epochs (see "Why one
  horizon instead of per-checkpoint epochs"): an already-marked subtree is
  exactly one whose treeobj is stamped at or above its checkpoint's epoch.
  Crucially this cache must be driven by GC's own traversal state, **not** by a
  bare epoch test: a treeobj stored by a recent commit has
  :math:`\text{epoch} \geq H` while still pointing at unchanged children with
  :math:`\text{epoch} < H` (a commit does not propagate epochs to deduplicated
  children), so :math:`\text{epoch} \geq H` does not imply the subtree is
  marked.
- **Lightweight "touch epoch" backing op.** A dedup re-store currently
  re-transfers and re-compresses the full blob only to bump its epoch. A
  hash-only "touch" op would avoid that. Deferred: dedup re-stores are expected
  to be rare, so the redundant transfer is not a concern in practice.
- **Rate limiting.** The time between sweep batches is not currently
  metered. If GC impact becomes a concern, run with smaller batch sizes or
  schedule during low-activity periods.
