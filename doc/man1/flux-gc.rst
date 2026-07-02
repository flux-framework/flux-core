===========
flux-gc(1)
===========


SYNOPSIS
========

**flux** **gc** [*OPTIONS*]


DESCRIPTION
===========

.. program:: flux gc

The :program:`flux gc` command performs online garbage collection on the
content backing store, reclaiming space from unreferenced blobs while the
Flux instance continues to run.

As the KVS evolves through commits, old versions of the hash tree accumulate
in the backing store. This command identifies and deletes blobs that are no
longer reachable from any live root, freeing disk space without requiring
downtime.

The garbage collection algorithm operates in three phases:

1. **Freeze the horizon**: Read the current checkpoint epoch and freeze it
   as the horizon *H*.

2. **Mark phase**: Walk the KVS tree from all live roots and mark every
   reachable blob by refreshing its epoch to *H*. Live roots include:

   - All retained checkpoints from the content backing store
   - All current private namespace roots (per-job KVS namespaces)

3. **Sweep phase**: Delete blobs from the backing store that have
   epoch < *H* in batches, leaving recently stored blobs and all marked
   blobs intact.

The horizon *H* is what makes garbage collection safe on a live, actively
committing instance: any blob stored or re-referenced by a concurrent commit
gets epoch >= *H* and is never swept, even if the mark phase hasn't reached it
yet.

Garbage collection is **conservative by design**. Some garbage may survive
multiple GC cycles if it was stored between mark and sweep, or if a private
namespace was created after root enumeration. This is acceptable—the important
invariant is that no referenced blob is ever deleted.


OPTIONS
=======

.. option:: -h, --help

   Summarize available options.

.. option:: -v, --verbose

   Increase verbosity. Specify multiple times for more detailed progress
   reporting.

.. option:: -n, --dry-run

   Report the count of reclaimable blobs without actually deleting them.
   Useful for estimating how much space would be freed.

.. option:: --no-cache

   Bypass the broker content cache and interact directly with the backing
   store. This avoids cache pollution during mark phase traversal.

.. option:: --maxreqs=N

   Set the maximum number of content requests kept in flight while walking
   the KVS during the mark phase (default 2). The default is deliberately low
   to avoid competing with a live instance's traffic for the content backing
   store. An off-peak run (for example from cron) can raise this to speed up
   the walk of a large KVS; mark throughput saturates by a window of about 16.


COMPARISON TO OFFLINE GC
=========================

**Online GC** (:program:`flux gc`):

- Runs while the instance is live
- Reclaims garbage incrementally
- Conservative: some garbage may survive

**Offline GC** (:man1:`flux-shutdown` ``--gc``):

- Runs during instance shutdown
- Dumps current KVS snapshot and rebuilds store from scratch
- Maximum reclamation: only reachable data remains
- Requires downtime
- Requires additional free disk space to hold the dump while the store is
  rebuilt, so it is a poor fit when space is already critically low. Online
  GC deletes in place and needs no scratch space.


RESOURCES
=========

Flux: http://flux-framework.org

RFC 11: Key Value Store Tree Object Format:
https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_11.html


SEE ALSO
========

:man1:`flux-dump`, :man1:`flux-restore`, :man1:`flux-shutdown`,
:man5:`flux-config-kvs`
