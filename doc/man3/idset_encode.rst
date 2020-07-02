===============
idset_encode(3)
===============


SYNOPSIS
========

::

   #include <flux/idset.h>

::

   char *idset_encode (const struct idset *idset, int flags);

::

   struct idset *idset_decode (const char *s);

::

   struct idset *idset_ndecode (const char *s, size_t len);

::

   typedef int (*idset_format_map_f)(const char *s, bool *stop, void *arg);

::

   int idset_format_map (const char *s, idset_format_map_f fun, void *arg);


USAGE
=====

cc [flags] files -lflux-idset [libraries]


DESCRIPTION
===========

Refer to ``idset_create(3)`` for a general description of idsets.

``idset_encode()`` creates a string from *idset*. The string contains
a comma-separated list of ids, potentially modified by *flags*
(see FLAGS below).

``idset_decode()`` creates an idset from a string *s*. The string may
have been produced by ``idset_encode()``. It must consist of comma-separated
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

``idset_ndecode()`` creates an idset from a sub-string *s* defined by
length *len*.

``idset_format_map()`` expands bracketed idset string(s) in *s*, calling
a map function *fun()* for each expanded string. The map function should
return 0 on success, or -1 on failure with errno set. Returning -1 causes
``idset_format_map()`` to immediately return -1. The map function may may
halt iteration without triggering an error by setting \*stop = true.

This function recursively expands multiple bracketed idset strings from
left to right, so for example, "r[0-1]n[0-1]" expands to "r0n0", "r0n1",
\* "r1n0", "r1n1".


FLAGS
=====

IDSET_FLAG_BRACKETS
   Valid for ``idset_encode()`` only. If set, the encoded string will be
   enclosed in brackets, unless the idset is a singleton (contains only
   one id).

IDSET_FLAG_RANGE
   Valid for ``idset_encode()`` only. If set, any consecutive ids are
   compressed into hyphenated ranges in the encoded string.


RETURN VALUE
============

``idset_decode()`` and ``idset_ndecode()`` return idset on success which must
be freed with ``idset_destroy(3)``. On error, NULL is returned with errno set.

``idset_encode()`` returns a string on success which must be freed
with ``free()``. On error, NULL is returned with errno set.

``idset_format_map()`` returns the number of times the map function was called
(including the stopping one, if any), or -1 on failure with errno set.


ERRORS
======

EINVAL
   One or more arguments were invalid.

ENOMEM
   Out of memory.


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

idset_create(3)

`RFC 22: Idset String Representation <https://github.com/flux-framework/rfc/blob/master/spec_22.rst>`__
