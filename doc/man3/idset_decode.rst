===============
idset_decode(3)
===============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/idset.h>

   struct idset *idset_decode (const char *s);

Link with :command:`-lflux-idset`.

DESCRIPTION
===========

Refer to :man3:`idset_create` for a general description of idsets.

:func:`idset_decode` creates an idset from a string :var:`s`. The string may
have been produced by :func:`idset_encode`. It must consist of comma-separated
non-negative integer ids, and may also contain hyphenated ranges.
If enclosed in square brackets, the brackets are ignored. Some examples
of valid input strings are:

::

   1,2,5,4
   1-4,7,9-10
   42
   [99-101]


RETURN VALUE
============

:func:`idset_decode` returns an idset on success which must be freed with
:man3:`idset_destroy`. On error, NULL is returned with :var:`errno` set.


ERRORS
======

EINVAL
   One or more arguments were invalid.

ENOMEM
   Out of memory.


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_22`


SEE ALSO
========

:man3:`idset_create`
