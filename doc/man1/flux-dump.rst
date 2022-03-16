.. flux-help-description: Write KVS snapshot to portable archive

============
flux-dump(1)
============


SYNOPSIS
========

**flux** **dump** [*OPTIONS*] *OUTFILE*


DESCRIPTION
===========

The ``flux-dump`` command writes a snapshot of the Flux key value store
to a file in a portable archive format.

The snapshot is of the primary namespace of the current KVS root by default.
If *--checkpoint* is specified, the snapshot is of the last KVS checkpoint
written to the content backing store.

The format of the archive and compression algorithm, if any, is determined
by the output file name extension.  The supported formats and compression
algorithms are a capability of :linux:man3:`libarchive`.  Some common
extensions that are supported by modern versions of that library are:

.tar
   POSIX *ustar* format, compatible with :linux:man1:`tar`.

.tgz, .tar.gz
   POSIX *ustar* format, compressed with :linux:man1:`gzip`.

.tar.bz2
   POSIX *ustar* format, compressed with :linux:man1:`bzip2`.

.tar.xz
   POSIX *ustar* format, compressed with :linux:man1:`xz`.

.zip
   ZIP archive, compatible with :linux:man1:`unzip`.

.cpio
   POSIX CPIO format, compatible with :linux:man1:`cpio`.

.iso
   ISO9660 CD image

The output filename may be *-* to indicate standard output.  In that case,
uncompressed tar format is used.


OPTIONS
=======

**-h, --help**
   Summarize available options.

**-v, --verbose**
   List keys on stderr as they are dumped instead of a periodic count of
   dumped keys.

**-q, --quiet**
   Don't show periodic count of dumped keys on stderr.

**--checkpoint**
   Generate snapshot from the latest checkpoint written to the content
   backing store, instead of from the current KVS root.

**--no-cache**
   Bypass the broker content cache and interact directly with the backing
   store.  This may be slightly faster, depending on how frequently the same
   content blobs are referenced by multiple keys.


OTHER NOTES
===========

KVS commits are atomic and propagate to the root of the namespace.  Because of
this, when ``flux-dump`` archives a snapshot of a live system, it reflects one
point in time, and does not include any changes committed while the dump is
in progress.

Since ``flux-dump`` generates the archive by interacting directly with the
content store, the *--checkpoint* option may be used to dump the most recent
state of the KVS when the KVS module is not loaded.

Only regular values and symbolic links are dumped to the archive.  Directories
are not dumped as independent objects, so empty directories are omitted from
the archive.

KVS symbolic links represent the optional namespace component in the target
as a *NAME::* prefix.

The KVS path separator is converted to the UNIX-compatible slash so that the
archive can be unpacked into a file system if desired.

The modification time of files in the archive is set to the time that
``flux-dump`` is started if dumping the current KVS root, or to the timestamp
of the checkpoint if *--checkpoint* is used.

The owner and group of files in the archive are set to the credentials of the
user that ran ``flux-dump``.

The mode of files in the archive is set to 0644.


RESOURCES
=========

Flux: http://flux-framework.org

RFC 10: Content Storage Service: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_10.html

RFC 11: Key Value Store Tree Object Format v1: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_11.html




SEE ALSO
========

:man1:`flux-restore`, :man1:`flux-kvs`
