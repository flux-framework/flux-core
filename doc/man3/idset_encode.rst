===============
idset_encode(3)
===============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/idset.h>

   char *idset_encode (const struct idset *idset, int flags);

Link with :command:`-lflux-idset`.

DESCRIPTION
===========

Refer to :man3:`idset_create` for a general description of idsets.

:func:`idset_encode` creates a string from :var:`idset`. The string contains
a comma-separated list of ids, potentially modified by :var:`flags`
(see FLAGS below).


FLAGS
=====

IDSET_FLAG_BRACKETS
   If set, the encoded string will be enclosed in brackets, unless the idset
   is a singleton (contains only one id).

IDSET_FLAG_RANGE
   If set, any consecutive ids are compressed into hyphenated ranges in the
   encoded string.


RETURN VALUE
============

:func:`idset_encode` returns a string on success which must be freed
with :linux:man3:`free`. On error, NULL is returned with :var:`errno` set.


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
