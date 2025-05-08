============
flux-fsck(1)
============


SYNOPSIS
========

**flux** **fsck** [*OPTIONS*]


DESCRIPTION
===========

.. program flux fsck

The :program:`flux fsck` checks the integrity of data stored in the content store.  In
particular, it will verify that all references listed in the content store exist and
are not corrupted.

OPTIONS
=======

.. option:: -h, --help

   Summarize available options.

.. option:: -v, --verbose

   List keys on stderr as they are validated.


EXIT STATUS
===========

0
  Content store valid

1
  One or errors were discovered


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
