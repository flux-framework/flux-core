===============
flux-filemap(1)
===============


SYNOPSIS
========

| **flux** **filemap** **map** [*--tags=LIST*] [*-C DIR*] *PATH* ...
| **flux** **filemap** **list** [*--tags=LIST*] [*--long*] [*PATTERN*]
| **flux** **filemap** **unmap** [*--tags=LIST*]
| **flux** **filemap** **get** [*--tags=LIST*] [*-C DIR*] [*PATTERN*]


DESCRIPTION
===========

.. program:: flux filemap

:program:`flux filemap` uses :linux:man2:`mmap` to map files into the rank 0
broker *content cache*.  After mapping, the files may be extracted on any
broker rank, taking advantage of scalability properties of the distributed
cache to move the data.  The files are treated as read-only and must not
change while mapped.

The ``stage-in`` shell plugin described in :man1:`flux-shell` may be used to
extract previously mapped files into the directory referred to by
:envvar:`FLUX_JOB_TMPDIR` or another directory.


COMMANDS
========

map
---

.. program:: flux filemap map

:program:`flux filemap map` maps one or more file *PATH* arguments.  It must be
run on the rank 0 broker, such as within a batch script, and the files must be
directly accessible by the rank 0 broker.  If a *PATH* refers to a directory,
the directory is recursively mapped.  If a file is encountered that is not
readable, or has a type other than regular file, directory, or symbolic link,
a fatal error occurs.

Sparse files such as file system images for virtual machines are mapped
efficiently.

File discretionary access permission are preserved, but file attributes,
ACLs, and group ownership are not.

.. option:: -C, --directory=DIR

   Change to the specified directory before performing the operation.

.. option:: -v, --verbose=[LEVEL]

   Increase output verbosity.

.. option:: --chunksize=N

   Limit the content mapped blob size to N bytes.  Set to 0 for unlimited.
   N may be specified as a floating point number with multiplicative suffix
   k,K=1024, M=1024\*1024, or G=1024\*1024\*1024 up to ``INT_MAX``.
   The default is 1M.

.. option:: --small-file-threshold=N

   Set the threshold in bytes over which a regular file is mapped through
   the distributed content cache. Set to 0 to always use the content cache.
   N may be specified as a floating point number with multiplicative suffix
   k,K=1024, M=1024\*1024, or G=1024\*1024\*1024 up to ``INT_MAX``.
   The default is 4K.

.. option:: --disable-mmap

   Never map a regular file through the distributed content cache.

.. option:: -T, --tags=LIST

   Specify a comma separated list of *tags*.  If no tags are specified,
   the *main* tag is assumed.


list
----

.. program:: flux filemap list

:program:`flux filemap list` lists mapped files.  Optionally, a :man7:`glob`
pattern may be specified to filter the list.

.. option:: -l, --long

   Include more detail in file listing.

.. option:: --blobref

   List blobrefs.

.. option:: --raw

   List RFC 37 file system objects.

.. option:: -T, --tags=LIST

   Specify a comma separated list of *tags*.  If no tags are specified,
   the *main* tag is assumed.

get
---

.. program:: flux filemap get

:program:`flux filemap get` extracts mapped files and may be run on any broker
or across all brokers using :man1:`flux-exec`.  Optionally, a :man7:`glob`
pattern may be specified to filter the list.  When extracting mapped files in
parallel, take care to specify a :option:`--directory` that is not shared and
is not on a network file system without considering the ramifications.

.. option:: -C, --directory=DIR

   Change to the specified directory before performing the operation.

.. option:: -v, --verbose=[LEVEL]

   Increase output verbosity.

.. option:: --direct

   Avoid indirection through the content cache when fetching the top level
   data for each file.  This may be fastest for a single or small number of
   clients, but will scale poorly when performed in parallel.

.. option:: -T, --tags=LIST

   Specify a comma separated list of *tags*.  If no tags are specified,
   the *main* tag is assumed.

.. option:: --overwrite

   Overwrite existing files when extracting.  :program:`flux filemap get`
   normally refuses to do this and treats it as a fatal error.

unmap
-----

.. program:: flux filemap unmap

:program:`flux filemap unmap` unmaps mapped files.

.. option:: -T, --tags=LIST

   Specify a comma separated list of *tags*.  If no tags are specified,
   the *main* tag is assumed.


EXAMPLE
=======

Example 1:  a batch script that copies data from ``/project/dataset1`` to a
temporary directory on each node of the batch allocation where it can be used
by multiple jobs.

.. code-block:: console

  #!/bin/bash

  flux filemap map -C /project dataset1
  flux exec -r all mkdir -p /tmp/project
  flux exec -r all flux filemap get -C /tmp/project

  # app1 and app2 have access to local copy of dataset1
  flux run -N1024 app1 --input /tmp/project/dataset1
  flux run -N1024 app2 --input /tmp/project/dataset1

  # clean up
  flux exec -r all rm -rf /tmp/project
  flux filemap unmap

Example 2: a batch script that maps two data sets with tags, then uses the
``stage-in`` shell plugin to selectively copy them to :envvar:`FLUX_JOB_TMPDIR`
which is automatically cleaned up after each job.

.. code-block:: console

  #!/bin/bash

  flux filemap map --tags=ds1 -C /project dataset1
  flux filemap map --tags=ds2 -C /project dataset2

  # App0 uses $FLUX_JOB_TMPDIR/dataset1 and $FLUX_JOB_TMPDIR/dataset2
  flux run -N1024 -o stage-in.tags=ds1,ds2 App0

  # App1 uses only $FLUX_JOB_TMPDIR/dataset1
  flux run -N1024 -o stage-in.tags=ds1 App1

  # App2 uses only $FLUX_JOB_TMPDIR/dataset2
  flux run -N1024 -o stage-in.tags=ds2 App2

  # clean up
  flux filemap unmap --tags=ds1,ds2

CAVEATS
=======

The rank 0 Flux broker may die with a SIGBUS error if a mapped file is removed
or truncated, and subsequently accessed, since that renders pages mapped into
the brokers address space invalid.

If mapped file content changes, access may fail if the original data is not
cached.  Under no circumstances will the new content be returned.

RESOURCES
=========

.. include:: common/resources.rst

FLUX RFC
========

:doc:`rfc:spec_37`


SEE ALSO
========

:man1:`flux-shell`,
