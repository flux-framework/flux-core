===============
idset_encode(3)
===============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/idset.h>

   char *idset_encode (const struct idset *idset, int flags);

   struct idset *idset_decode (const char *s);

   struct idset *idset_ndecode (const char *s, size_t len);

Link with :command:`-lflux-idset`.

DESCRIPTION
===========

Refer to :man3:`idset_create` for a general description of idsets.

:func:`idset_encode` creates a string from :var:`idset`. The string contains
a comma-separated list of ids, potentially modified by :var:`flags`
(see FLAGS below).

:func:`idset_decode` creates an idset from a string :var:`s`. The string may
have been produced by :func:`idset_encode`. It must consist of comma-separated
non-negative integer ids, and may also contain hyphenated ranges.
If enclosed in square brackets, the brackets are ignored. Some examples
of valid input strings are:

::

   1,2,5,4

::

   1-4,7,9-10

::

   42

::

   [99-101]

:func:`idset_ndecode` creates an idset from a sub-string :var:`s` defined by
length :var:`len`.


FLAGS
=====

IDSET_FLAG_BRACKETS
   Valid for :func:`idset_encode` only. If set, the encoded string will be
   enclosed in brackets, unless the idset is a singleton (contains only
   one id).

IDSET_FLAG_RANGE
   Valid for :func:`idset_encode` only. If set, any consecutive ids are
   compressed into hyphenated ranges in the encoded string.


RETURN VALUE
============

:func:`idset_decode` and :func:`idset_ndecode` return idset on success which
must be freed with :man3:`idset_destroy`. On error, NULL is returned with
:var:`errno` set.

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

Flux: http://flux-framework.org

RFC 22: Idset String Representation: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_22.html


SEE ALSO
========

:man3:`idset_create`
