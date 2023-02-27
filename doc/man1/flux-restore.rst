===============
flux-restore(1)
===============


SYNOPSIS
========

**flux** **restore** [*OPTIONS*] *INFILE*


DESCRIPTION
===========

The ``flux-restore`` command reads a KVS snapshot from a portable archive
format, usually written by :man1:`flux-dump`.

The archive source may be specified as a file path or *-* for standard input.
The format of the archive may be any of the formats supported by
:linux:man3:`libarchive` and is determined on the fly based on the archive
content.

The snapshot may be restored to a KVS key if *--key=NAME* is used and the
KVS service is running, or as a checkpoint in the content backing store
if *--checkpoint* is used, without the KVS running.  One of those two options
is required.


OPTIONS
=======

**-h, --help**
   Summarize available options.

**-v, --verbose**
   List keys on stderr as they are restored instead of a periodic count of
   restored keys.

**-q, --quiet**
   Don't show a periodic count of restored keys on stderr.

**--checkpoint**
   After restoring the archived content, write the final root blobref
   to the KVS checkpoint area in the content backing store.  The checkpoint
   is used as the initial KVS root when the KVS module is loaded.  Unload
   the KVS module before restoring with this option.

**--key**\ =\ *NAME*
   After restoring the archived content, write the final root blobref
   to a KVS key, so the key becomes the restored root directory.

**--no-cache**
   Bypass the broker content cache and interact directly with the backing
   store.  Performance will vary depending on the content of the archive.


RESOURCES
=========

Flux: http://flux-framework.org

RFC 10: Content Storage Service: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_10.html

RFC 11: Key Value Store Tree Object Format v1: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_11.html


SEE ALSO
========

:man1:`flux-dump`, :man1:`flux-kvs`
