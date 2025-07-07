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

   Don't output diagnostic messages and discovered errors.

.. option:: -r, --rootref=BLOBREF

   Normally the check starts with the blobref in the most recent KVS
   checkpoint.  This option directs flux-fsck to start at an arbitrary
   point.  BLOBREF must refer to an RFC 11 tree object of type "dir".


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


SEE ALSO
========

:man1:`flux-content`, :man1:`flux-kvs`
