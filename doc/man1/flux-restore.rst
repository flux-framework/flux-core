===============
flux-restore(1)
===============


SYNOPSIS
========

**flux** **restore** [*OPTIONS*] *INFILE*


DESCRIPTION
===========

.. program:: flux restore

The :program:`flux restore` command reads a KVS snapshot from a portable
archive format, usually written by :man1:`flux-dump`.

The archive source may be specified as a file path or *-* for standard input.
The format of the archive may be any of the formats supported by
:linux:man3:`libarchive` and is determined on the fly based on the archive
content.

The snapshot may be restored to a KVS key if :option:`--key=NAME` is used and
the KVS service is running, or as a checkpoint in the content backing store
if :option:`--checkpoint` is used, without the KVS running.  One of those two
options is required.


OPTIONS
=======

.. option:: -h, --help

   Summarize available options.

.. option:: -v, --verbose

   List keys on stderr as they are restored instead of a periodic count of
   restored keys.

.. option:: -q, --quiet

   Don't show a periodic count of restored keys on stderr.

.. option:: --checkpoint

   After restoring the archived content, write the final root blobref
   to the KVS checkpoint area in the content backing store.  The checkpoint
   is used as the initial KVS root when the KVS module is loaded.  Unload
   the KVS module before restoring with this option.

.. option:: --key=NAME

   After restoring the archived content, write the final root blobref
   to a KVS key, so the key becomes the restored root directory.

.. option:: --no-cache

   Bypass the broker content cache and interact directly with the backing
   store.  Performance will vary depending on the content of the archive.

.. option:: --size-limit=SIZE

   Skip restoring keys that exceed SIZE bytes (default: no limit). SIZE may
   be specified as a floating point number with an optional multiplicative
   suffix k or K=1024, M=1024\*1024, or G=1024\*1024\*1024 (up to
   ``INT_MAX``).

.. option:: --sd-notify

   Regularly inform the broker of progress so it can provide human readable
   progress for :program:`systemctl status flux`.

RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_10`

:doc:`rfc:spec_11`


SEE ALSO
========

:man1:`flux-dump`, :man1:`flux-kvs`
