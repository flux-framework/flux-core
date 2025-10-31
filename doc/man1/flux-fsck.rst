============
flux-fsck(1)
============


SYNOPSIS
========

**flux** **fsck** [*OPTIONS*]


DESCRIPTION
===========

.. program flux fsck

The :program:`flux fsck` checks the integrity of the KVS backing
store.  By default, it starts with the most recent checkpoint (root version)
written to the backing store.


OPTIONS
=======

.. option:: -h, --help

   Summarize available options.

.. option:: -v, --verbose

   List keys on stderr as they are validated.

.. option:: -q, --quiet

   Reduce output to essential information.  Can significantly reduce
   output if output will be logged.

.. option:: -r, --rootref=BLOBREF

   Normally the check starts with the blobref in the most recent KVS
   checkpoint.  This option directs flux-fsck to start at an arbitrary
   point.  BLOBREF must refer to an RFC 11 tree object of type "dir".

.. option:: -R, --repair

   Remove any dangling references found in KVS metadata. If a KVS
   value changes as a result of this repair, the key is moved to the
   lost+found directory.  The original key is unlinked.  If a key
   points to a KVS directory that is no longer valid, it is
   unlinked. All damaged keys and their disposition are listed on
   stderr. This process creates a new root reference for these
   changes, and commits it as the current KVS checkpoint at the end of
   the scan. The KVS is required to be unloaded during repair.

   This option should be considered :ref:`EXPERIMENTAL <fsck_experimental>`
   at this time.


EXIT STATUS
===========

0
  Content store valid

1
  One or more errors were discovered


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_10`

:doc:`rfc:spec_11`


.. _fsck_experimental:
EXPERIMENTAL
============

.. include:: common/experimental.rst


SEE ALSO
========

:man1:`flux-content`, :man1:`flux-kvs`
