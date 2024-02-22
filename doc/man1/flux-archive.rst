===============
flux-archive(1)
===============


SYNOPSIS
========

| **flux** **archive** **create** [*-n NAME*] [*-C DIR*] *PATH* ...
| **flux** **archive** **list** [*-n NAME*] [*--long*] [*PATTERN*]
| **flux** **archive** **extract** [*-n NAME*] [*-C DIR*] [*PATTERN*]
| **flux** **archive** **remove** [*-n NAME*] [*-f*]


DESCRIPTION
===========

.. program:: flux archive

:program:`flux archive` stores multiple files in an RFC 37 archive
under a single KVS key.  The archive can be efficiently extracted in
parallel across the Flux instance, leveraging the scalability properties
of the KVS and its content addressable data store.

Sparse files such as file system images for virtual machines are archived
efficiently.

File discretionary access permissions are preserved, but file attributes,
ACLs, and group ownership are not.

The ``stage-in`` shell plugin described in :man1:`flux-shell` may be used to
extract previously archived files into the directory referred to by
:envvar:`FLUX_JOB_TMPDIR` or another directory.

Due to the potential impact on Flux's storage footprint on rank 0, this
command is limited to instance owners, e.g. it works in batch jobs and
allocations but not at the system level.


COMMANDS
========

create
------

.. program:: flux archive create

:program:`flux archive create` archives one or more file *PATH* arguments.
If a *PATH* refers to a directory, the directory is recursively archived.
If a file is encountered that is not readable, or has a type other than
regular file, directory, or symbolic link, a fatal error occurs.

.. option:: -n, --name=NAME

   Specify the archive name.  If a name is not specified, ``main`` is used.

   The archive will be committed to the KVS as ``archive.NAME``.

.. option:: --overwrite

   Unlink ``archive.NAME`` and ``archive.NAME_blobs`` from the KVS, and
   unmap all files associated with *NAME* that were previously archived
   with :option:`--mmap` before creating the archive.

   Without :option:`--overwrite` or :option:`--append`, it is a fatal error
   if *NAME* exists.

.. option:: --append

   If *NAME* exists, append new content to the existing archive.
   Otherwise create it from scratch.

   Due to the way the KVS handles key changes, appending :math:`M` bytes to
   to a key of size :math:`N` consumes roughly :math:`2N + M` bytes in the
   backing store, while separate keys consume :math:`N + M`.  As a consequence,
   creating multiple archives may be cheaper than building one iteratively
   with :option:`--append`.

.. option:: -C, --directory=DIR

   Change to the specified directory before performing the operation.

.. option:: --no-force-primary

   Create the archive in the default KVS namespace, honoring
   :envvar:`FLUX_KVS_NAMESPACE`, if set.  By default, the primary KVS
   namespace is used.

.. option:: --preserve

   Write additional KVS metadata so that the archive remains intact across
   a Flux restart with garbage collection.

   The metadata will be committed to the KVS as ``archive.NAME_blobs``.

.. option:: -v, --verbose=[LEVEL]

   List files on standard error as the archive is created.

.. option:: --chunksize=N

   Limit the content blob size to N bytes.  Set to 0 for unlimited.
   N may be specified as a floating point number with multiplicative suffix
   k,K=1024, M=1024\*1024, or G=1024\*1024\*1024 up to ``INT_MAX``.
   The default is 1M.

.. option:: --small-file-threshold=N

   Set the threshold in bytes for a small file.  A small file is represented
   directly in the archive, as opposed to the content store.  Set to 0 to
   always use the content store.  N may be specified as a floating point
   number with multiplicative suffix k,K=1024, M=1024\*1024, or
   G=1024\*1024\*1024 up to ``INT_MAX``.  The default is 1K.

.. option:: --mmap

   For large files, use :linux:man2:`mmap` to map file data into the content
   store rather than copying it.  This only works on rank 0, and does not work
   with :option:`--preserve` or :option:`--no-force-primary`.  Furthermore,
   the files must remain available and unchanged while the archive exists.
   This is most appropriate for truly large files such as VM images.

   .. warning::

      The rank 0 Flux broker may die with a SIGBUS error if a mapped file is
      removed or truncated, and subsequently accessed, since that renders
      pages mapped into the brokers address space invalid.

      If mapped file content changes, access may fail if the original data
      is not cached, but under no circumstances will the new content be
      returned.

list
----

.. program:: flux archive list

:program:`flux archive list` shows the archive contents on standard output.
If *PATTERN* is specified, only the files that match the :linux:man7:`glob`
pattern are listed.

.. option:: -l, --long

   List the archive in long form, including file type, mode, and size.

.. option:: --raw

   List the RFC 37 file objects in JSON form, without decoding.

.. option:: -n, --name=NAME

   Specify the archive name.  If a name is not specified, ``main`` is used.

.. option:: --no-force-primary

   List the archive in the default KVS namespace, honoring
   :envvar:`FLUX_KVS_NAMESPACE`, if set.  By default, the primary KVS
   namespace is used.

remove
------

.. program:: flux archive remove

:program:`flux archive remove` expunges an archive.  The archive's KVS keys
are unlinked, and any files previously mapped with :option:`--mmap` are
unmapped.

.. option:: -n, --name=NAME

   Specify the archive name.  If a name is not specified, ``main`` is used.

.. option:: --no-force-primary

   Remove the archive in the default KVS namespace, honoring
   :envvar:`FLUX_KVS_NAMESPACE`, if set.  By default, the primary KVS
   namespace is used.

.. option:: -f, --force

   Don't fail if the archive does not exist.


extract
-------

.. program:: flux archive extract

:program:`flux archive extract` extracts files from a KVS archive.
If *PATTERN* is specified, only the files that match the :linux:man7:`glob`
pattern are extracted.

.. option:: -t, --list-only

   List files in the archive without extracting.

.. option:: -n, --name=NAME

   Specify the archive name.  If a name is not specified, ``main`` is used.

.. option:: -C, --directory=DIR

   Change to the specified directory before performing the operation.

   When extracting files in parallel, take care when specifying *DIR*:

   - It should have enough space to hold the extracted files.

   - It should not be a fragile network file system such that parallel
     writes could cause a distributed denial of service.

   - It should not already be shared among the nodes of your job.

.. option:: -v, --verbose=[LEVEL]

   List files on standard error as the archive is extracted.

.. option:: --overwrite

   Overwrite existing files when extracting.  :program:`flux archive extract`
   normally refuses to do this and treats it as a fatal error.

.. option:: --waitcreate[=FSD]

   Wait for the archive key to appear in the KVS if it doesn't exist.
   This may be necessary in some circumstances as noted in `CAVEATS`_
   below.

   If *FSD* is specified, it is interpreted as a timeout value in RFC 23
   Flux Standard Duration format.

.. option:: --no-force-primary

   Extract from the archive in the default KVS namespace, honoring
   :envvar:`FLUX_KVS_NAMESPACE`, if set.  By default, the primary KVS
   namespace is used.

CAVEATS
=======

The KVS employs an "eventually consistent" cache update model, which
means one has to be careful when writing a key on one broker rank and
reading it on other broker ranks.  Without some form of synchronization,
there is a short period of time where the KVS cache on the other ranks
may not yet have the new data.

This is not an issue for Example 2 below, where a batch script creates
an archive, then submits jobs that read the archive because job
submission and execution already include KVS synchronization.
In other situations such as Example 1, it is advisable to use
:option:`--waitcreate` or to explicitly synchronize between writing
the archive and reading it, e.g.

.. code-block:: console

    flux exec -r all flux kvs wait $(flux kvs version)


EXAMPLES
========

Example 1:  a batch script that archives data from ``/project/dataset1``, then
replicates it in a temporary directory on each node of the batch allocation
where it can be used by multiple jobs.

.. code-block:: console

  #!/bin/bash

  flux archive create -C /project dataset1
  flux exec -r all mkdir -p /tmp/project
  flux exec -r all flux archive extract --waitcreate -C /tmp/project

  # app1 and app2 have access to local copy of dataset1
  flux run -N1024 app1 --input=/tmp/project/dataset1
  flux run -N1024 app2 --input=/tmp/project/dataset1

  # clean up
  flux exec -r all rm -rf /tmp/project
  flux archive remove

Example 2: a batch script that archives a large executable and a data set,
then uses the ``stage-in`` shell plugin to copy them to
:envvar:`FLUX_JOB_TMPDIR` which is automatically cleaned up after each job.

.. code-block:: console

  #!/bin/bash

  flux archive create --name=dataset1 -C /project dataset1
  flux archive create --name=app --mmap -C /home/fred app

  flux run -N1024 -o stage-in.names=app,dataset1 \
      {{tmpdir}}/app --input={{tmpdir}}/dataset1

  # clean up
  flux archive remove --name=dataset1
  flux archive remove --name=app


RESOURCES
=========

.. include:: common/resources.rst

FLUX RFC
========

| :doc:`rfc:spec_16`
| :doc:`rfc:spec_23`
| :doc:`rfc:spec_37`


SEE ALSO
========

:man1:`flux-shell`, :man1:`flux-kvs`, :man1:`flux-exec`
