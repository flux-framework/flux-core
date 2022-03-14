===============
flux-content(1)
===============


SYNOPSIS
========

**flux** **content** **load** [*--bypass-cache*] *blobref*

**flux** **content** **store** [*--bypass-cache*]

**flux** **content** **flush**

**flux** **content** **dropcache**

DESCRIPTION
===========

Each Flux instance implements an append-only, content addressable
storage service, which stores blobs of arbitrary content under
message digest keys termed "blobrefs".

**flux content store** accepts a blob on standard input, stores it,
and prints the blobref on standard output.

**flux content load** accepts a blobref argument, retrieves the
corresponding blob, and writes it to standard output.

After a store operation completes on any rank, the blob may be
retrieved from any other rank.

The content service includes a cache on each broker which improves
scalability. The **flux content flush** command initiates store requests
for any dirty entries in the local cache and waits for them to complete.
This is mainly used in testing. The **flux content dropcache** command
drops all non-essential entries in the local cache; that is, entries
which can be removed without data loss.

These operations are only available to the Flux instance owner.


OPTIONS
=======

**-b, --bypass-cache**
   Bypass the in-memory cache, and directly access the backing store,
   if available (see below).


BACKING STORE
=============

The rank 0 cache retains all content until a module providing
the "content.backing" service is loaded which can offload content
to some other place. The **content-sqlite** module provides this
service, and is loaded by default.

Content database files are stored persistently on rank 0 if the
persist-directory broker attribute is set to a directory name for
the session. Otherwise they are stored in the directory defined
by the rundir attribute and are cleaned up when the instance terminates.

When one of these modules is loaded, it informs the rank 0
cache of its availability, which triggers the cache to begin
offloading entries. Once entries are offloaded, they are eligible
for expiration from the rank 0 cache.

To avoid data loss, once a content backing module is loaded,
do not unload it unless the content cache on rank 0 has been flushed
and the system is shutting down.


CACHE EXPIRATION
================

The parameters affecting local cache expiration may be tuned with
flux-setattr(1):

**content.purge-target-size**
   The cache is purged to bring the sum of the size of cached blobs less
   than or equal to this value
   (default 16777216)

**content.purge-old-entry**
   Only entries that have not been accessed in **old-entry** seconds
   are eligible for purge (default 10).

Expiration becomes active on every heartbeat.  Dirty or invalid entries are
not eligible for purge.


CACHE ACCOUNTING
================

Some accounting info for the local cache can be viewed with :man1:`flux-getattr`:

**content.acct-entries**
   The total number of cache entries.

**content.acct-size**
   The sum of the size of cached blobs.

**content.acct-dirty**
   The number of dirty cache entries.

**content.acct-valid**
   The number of valid cache entries.


CACHE SEMANTICS
===============

The cache is write-through with respect to the rank 0 cache;
that is, a store operation does not receive a response until it
is valid in the rank 0 cache.

The cache on rank 0 is write-back with respect to the backing store,
if any; that is, a store operation may receive a response before
it has been stored on the backing store.

The cache is hierarchical. Rank 0 (the root of the tree based
overlay network) holds all blobs stored in the instance.
Other ranks keep only what a they heuristically determine to
be of benefit. On ranks > 0, a load operation that cannot be fulfilled
from the local cache is "faulted" in from the level above it.
A store operation that reaches a level that has already cached the
same content is "squashed"; that is, it receives a response without
traveling further up the tree.


RESOURCES
=========

Flux: http://flux-framework.org

RFC 10: Content Storage Service: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_10.html
