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

.. option:: -r, --rootref=<BLOBREF|index>

   Normally the check starts with the blobref in the most recent KVS
   checkpoint.  This option can direct flux-fsck to start at an
   arbitrary point by specifying a BLOBREF that points to an RFC 11
   tree object of type "dir".  An numerical index can be direct
   flux-fsck to start at an older checkpoint (i.e. 0 = use current checkpoint,
   1 = checkpoint before current checkpoint, ...).

.. option:: -c, --checkpoint

   If a root reference specified by :option:`--rootref` does not
   contain any errors, checkpoint that root reference to the be new
   checkpointed root reference.


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
