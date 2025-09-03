===============
flux-content(1)
===============


SYNOPSIS
========

| **flux** **content** **load** [*--bypass-cache*] [*blobref* ...]
| **flux** **content** **store** [*--bypass-cache*] [*--chunksize=N*]
| **flux** **content** **flush**
| **flux** **content** **dropcache**
| **flux** **content** **checkpoint** **list** [*-n*] [*--json*]
| **flux** **content** **checkpoint** **update** *blobref*


DESCRIPTION
===========

Each Flux instance implements an append-only, content addressable storage
service.  The content service stores blobs of arbitrary data under
"blobref" keys.  Blobrefs are derived from a hash of the data and thus can
be computed in advance and always refer to the same blob.

The leader broker (rank 0) holds the full data set, and normally offloads
blobs to a sqlite database on disk.  The database usually resides in the
broker ``rundir`` which is created anew when Flux starts, and is cleaned
up when Flux terminates.  However if the ``statedir`` broker attribute is
set, the database resides there and can persist across Flux restarts, but
see `CAVEATS`_ below.

The content service was designed for, and is primarily used by, the Flux KVS.
Access is restricted to the instance owner.


COMMANDS
========

store
-----

.. program:: flux content store

:program:`flux content store` reads data from standard input to EOF, stores it
(possibly splitting into multiple blobs), and prints blobref(s) on
standard output, one per line.

After a store operation completes on any rank, the blobs may be
retrieved from any other rank.

.. option:: -b, --bypass-cache

   Bypass the in-memory cache, and directly access the backing store,
   if available.

.. option:: --chunksize=N

   Split a blob into chunks of *N* bytes.

load
----

.. program:: flux content load

:program:`flux content load` reads blobrefs from standard input, one per line,
or parses blobrefs on the command line (but not both).  It then loads the
corresponding blob(s), and concatenates them on standard output.

.. option:: -b, --bypass-cache

   Bypass the in-memory cache, and directly access the backing store,
   if available.

flush
-----

.. program:: flux content flush

The content service includes a cache on each broker which improves
scalability. The :program:`flux content flush` command initiates store requests
for any dirty entries in the local cache and waits for them to complete.
This is mainly used in testing.

dropcache
---------

.. program:: flux content dropcache

The :program:`flux content dropcache` command drops all non-essential entries
in the local cache; that is, entries which can be removed without data loss.

checkpoint list
---------------

.. program:: flux content checkpoint list

The :program:`flux content checkpoint list` lists all checkpoints
currently stored.

.. option:: -n, --no-header

   Do not output column headers.

.. option:: -j, --json

   Output raw json checkpoint data.

checkpoint update
-----------------

.. program:: flux content checkpoint update

The :program:`flux content checkpoint update` updates the current checkpoint
to the specified *blobref*.


CAVEATS
=======

The KVS implements its hierarchical key space using a hash tree, where
the hashes refer to content entries.  As the KVS is used, the append-only
nature of the content service results in an accumulation of unreferenced
data.  In restartable Flux instances, this is mitigated by
:option:`flux shutdown --gc` offline garbage collection, where a dump of
the current KVS root snapshot is created at shutdown, and the content
database is removed and recreated from the dump at restart.  This presents
a problem for other users of the content service.  If content needs to be
preserved in this situation, the best recourse is to ensure it is linked
into the KVS hash tree before the instance is shut down. The
:option:`flux kvs put --treeobj` option is available for this purpose.

A large or long-running Flux instance might generate a lot of content
that is offloaded to ``rundir`` on the leader broker.  If the file system
(usually ``/tmp``) containing ``rundir`` is a ramdisk, this can lead to less
memory available for applications on the leader broker, or to catastrophic
failures if the file system fills up.  Some workarounds for batch jobs are::

  # exclude the leader (rank 0) broker from scheduling
  flux batch --conf=resource.exclude=\"0\"

  # redirect storage to a global file system (pre-create empty)
  flux batch --broker-opts=--setattr=statedir=/path/to/directory


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_10`

:doc:`rfc:spec_11`


SEE ALSO
========

:man1:`flux-kvs`, :man7:`flux-broker-attributes`
