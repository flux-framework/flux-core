.. _kvs:

###############
Key Value Store
###############

The Flux key value store is an essential building block for Flux services.
This page describes the current KVS design.

As described in
`SRMPDS 2014 <https://flux-framework.org/publications/Flux-SRMPDS-final.pdf>`_,
the Flux KVS is a general purpose data store used by other Flux components.
It stores values under a hierarchical key space with a single leader
node and multiple caching followers. The weak consistency of its follower
caches has the following properties, using the taxonomy from
`Vogel 2007 <http://www.allthingsdistributed.com/2007/12/eventually_consistent.html>`_:

Causal consistency
  If process A communicates with process B that it has updated a data item
  (passing a store version in that message), a subsequent access by process
  B will return the updated value.

Read-your-writes consistency
  A process having updated a data item, never accesses an older value.

Monotonic read consistency
  If a process has seen a particular value for an object, any subsequent
  accesses will never return previous values.

These properties are achieved with hash trees and content-addressable storage,
borrowing ideas from
`ZFS <https://www.cs.hmc.edu/~rhodes/cs134/readings/The%20Zettabyte%20File%20System.pdf>`_,
`camlistore <https://camlistore.org/>`_, and
`git <https://git-scm.com/book/en/v2/Git-Internals-Git-Objects>`_.
KVS values and metadata are placed in a content-addressable store,
indexed by their hash digests, or *blobrefs*.  The content store is described
in :doc:`RFC 10 <rfc:spec_10>` and briefly below.

KVS metadata consist of directories, symbolic links, and value references,
formatted as JSON objects.  These objects are described in
:doc:`RFC 11 <rfc:spec_11>` and briefly below.

**********
key lookup
**********

Path components of a key each reference a directory entry in a different
directory.  The first path component is looked up by following a root
blobref to the root directory, finding a matching entry, and following
that blobref to a new entry, and so on until the terminal name is reached
and the resulting object is returned to the user.

For example, if the root blobref is ``sha1-1c002dde`` and we have stored
*a.b.c = 42*, we would look it up as follows:

1. load root directory from ``sha1-1c002dde``, find *a* is at
   ``sha1-3f2243ef``
2. load *a* from ``sha1-3f2243ef``, find *b* is at ``sha1-023e9b2d``
3. load *b* from ``sha1-023e9b2d``, find *c* is at ``sha1-7ff234a8``
4. load *c* from ``sha1-7ff234a8``, and return the value: *42*.

It's actually slightly more complicated than that since large directories
and values can span multiple blobs.  Instead of always a single blobref,
directory entries contain a *dirref* or *valref* object that contain
arrays of blobrefs.  Following a *dirref* to the next *dir* object
may require fetching multiple blobs to reconstruct the next object.
Retrieving a value from a *valref* may similarly require fetching multiple
blobs to reconstruct the final value.  Finally, as an optimization,
small values may be included directly in a directory entry in a *val* object.

The basic KVS API for looking up a value by key is described in
:man3:`flux_kvs_lookup`.


**********
key update
**********

An important property of the hash tree structure is that any update
results in a new root blobref .  Continuing the example,
to update *a.b.c = 43*, we:

1. store the value *43* to ``sha1-62302aff``
2. update *b* to associate *c* with ``sha1-62302aff``, and
   store *b* to ``sha1-8fe9b2c3``
3. update *a* to associate *b* with ``sha1-8fe9b2c3``, and
   store *a* to ``sha1-aacc76b4``
4. update root to associate *a* with ``sha1-aacc76b4``, and
   store root to ``sha1-033fbe92``
5. the new root blobref is ``sha1-033fbe92``.

All updates are applied first on the leader node at the root of the TBON,
which then publishes a new root reference as a *setroot* event.  Followers keep
consistent with the leader by switching their root reference in response to
this event, so that all new lookups begin at the new root directory.

Updates are transactional in that multiple changes can be batched up into
a single *commit*, which either succeeds or fails entirely, without making
intermediate states visible to users.

The basic KVS API for updating keys is described in :man3:`flux_kvs_txn_create`
and :man3:`flux_kvs_commit`.

*************
content cache
*************

As mentioned above, a distinct distributed service, used by the KVS,
maps blobrefs to blobs, and implements a temporal cache on
each broker rank, backed ultimately by a sqlite database on rank 0.

When the KVS is performing a key lookup, tree objects are retrieved from
the content cache by blobref.  Blobs not present in the local
broker's content cache are faulted in from the TBON parent, recursing
up the tree until the request can be fulfilled.  Unused content cache
entries are expired from memory after a period of disuse.  They persist
in the sqlite database, which does not allow blobs to be deleted.

A store to the content cache is allowed to return once the entry is
present in the rank 0 memory cache, since this ensures it can be read
by any rank.  It must be written to sqlite before it can be expired
from the rank 0 memory cache.

The content store can be used independently, e.g. through the command line
as described in :man1:`flux-content` or or by sending the RPCs described
in :doc:`RFC 10 <rfc:spec_10>`.

**********
namespaces
**********

The KVS supports multiple namespaces.  The *primary namespace* is
always available and can only be accessed by the :term:`instance owner`.

The instance owner can create and destroy additional namespaces,
and assign each an *owner* who can access the namespace, in addition
to the instance owner which can access all namespaces.  In Flux, each job
is set up with its own namespace, owned by the job owner, who may be a
:term:`guest` user.

Although commits are serialized on a given namespace, commits on
distinct namespaces can progress in parallel.

***********
consistency
***********

Flux event messages are sequenced and guaranteed to be delivered in order.
This property, and the serialization of commits on the leader node, ensure
that monotonic read consistency is achieved.

Read-your-writes consistency is obtained by piggy-backing on the commit
response the new root blobref and its version number, a monotonic sequence.
As the response propagates from the leader node to the sender of the commit
through a sequence of followers, the followers update their root reference to
at least the version in the response.  Thus, once a commit response is
received, a lookup initiated by the caller will always return that version
of the store or newer.  Races with the setroot event are avoided by minding
the sequence number so root updates are never applied out of sequence.

Causal consistency is available programmatically.  After a commit, the root
blobref version can be read via :man3:`flux_kvs_commit_get_sequence`
or :func:`flux_kvs_get_version` and passed to another rank, which can use
:func:`flux_kvs_wait_version` to block until the follower root blobref
reaches that version, after which the data from the commit (or newer) can
be retrieved.  In summary:

1. Process A commits data, then gets the store version :math:`V` and sends
   it to B.
2. Process B waits for the store version to be :math:`>= V`, then reads data.

**********************
the secret other cache
**********************

In the current KVS implementation, there is a cache of content blobs
in the KVS module that sits in front of the content cache service.
Cached blobs are accessible directly rather than through RPC to the
local content cache service, except when there is a "fault",
and then access must be suspended while the RPC completes.  This local
cache is temporally expired like the content cache service.

When a request to look up a name is handled and a needed tree object
(for example a directory in the middle of a path) is not in cache, the
request message is queued on a cache entry that is waiting to be filled.
Once the cache entry has been filled, the request message queued on it is
"restarted" meaning handled like a new request.  Since all the tree objects
for the lookup up to that point should be in cache, it won't block until it
reaches the next missing tree object.  Multiple requests can be queued on
each cache entry, but only one content request is triggered regardless of
how many consumers there are for it.  This design was an optimization for
many requests looking up the same thing such as during a PMI exchange.

***********************
treeobj metadata format
***********************

:doc:`RFC 11 <rfc:spec_11>` defines the treeobj format, but here is a brief
summary:

A *valref* refers to opaque data in the content store (the actual data,
not a *val* object).

.. code-block:: json

  { "ver":1,
    "type":"valref",
    "data":["sha1-aaa...","sha1-bbb..."],
  }

A *val* represents opaque data directly, base64-encoded.

.. code-block:: json

  { "ver":1,
    "type":"val",
    "data":"NDIyCg==",
  }

A *dirref* refers to a *dir* or *hdir* object that was serialized and
stored in the content store.

.. code-block:: json

  { "ver":1,
    "type":"dirref",
    "data":["sha1-aaa...","sha1-bbb..."],
  }

A *dir* is a dictionary mapping keys to any of the tree object types.

.. code-block:: json

  { "ver":1,
    "type":"dir",
    "data":{
       "a":{"ver":1,"type":"dirref","data":["sha1-aaa"]},
       "b":{"ver":1,"type":"val","data":"NDIyCg=="},
       "c":{"ver":1,"type":"valref","data":["sha1-aaa","sha1-bbb"]},
       "d":{"ver":1,"type":"dir","data":{},
    }
  }

A *symlink* is a symbolic pointer to a another KVS key, which may
or may not be fully qualified.

.. code-block:: json

  { "ver":1,
    "type":"symlink",
    "data":"a.aa",
  }

*****
watch
*****

Any key (including the root directory) can be "watched", such that a user
receives responses for each key change.

Watch is implemented in a separate ``kvs-watch`` module.

Each time a setroot event indicates that something has changed, the
``kvs-watch`` module looks up the watched keys to see if they have changed.

Because watched keys have to be looked up from the root on *every* KVS
commit, watching a key has a high overhead, but some relief is given by
the optimization of including a list of changed keys in the setroot event,
described below.

The API for watching KVS keys is described in :man3:`flux_kvs_lookup`.
Basically a special flag is added and responses are received each time
the value changes or the request is canceled or the caller disconnects.

*************
optimizations
*************

piggyback root directory on setroot event
=========================================

The KVS design calls for publication of a setroot event each time
the root blobref changes.

Since most lookups will need to start with the root directory
(the object pointed to by the root blobref), the new root directory will
most likely not be in cache, and any watched keys will be looked up on
every commit, the root directory object is made part of the setroot event
so this lookup is avoided.

commit merging
==============

Since all updates are serialized through the rank 0 commit process,
this area of the code is an obvious target for optimization.
One such optimization is commit merging.

*commit merging* is implemented by queuing parsed commit requests
during the main part of the reactor loop, and processing them only
when the reactor has no events (request messages) pending, using
the prepare/check/idle watcher pattern.

This minimizes the work that is duplicated for each commit:  creating
a temporary in-memory json object to work on, and updating the root
reference and top level directories when the commit is finalized.
This saves time, but also reduces the number of intermediate objects
written to the content cache.

A problem arises when intermediate values for a key are overwritten
in merged commits, and a "watcher" needs to see each value to function
properly, such as to synchronize a state machine.  To solve this,
a ``KVS_NO_MERGE`` flag may be added to :func:`flux_kvs_commit`, which
indicates that the merge should not be subject to this optimization.

piggyback list of changed keys on setroot event
===============================================

To streamline the KVS watch implementation, a list of changed keys is
included in the kvs setroot event.  That way the entire list of watched keys
does not need to be looked up every time there is a new root.
