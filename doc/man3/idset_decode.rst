===============
idset_decode(3)
===============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/idset.h>

   typedef struct {
      char text[160];
   } idset_error_t;

   struct idset *idset_decode (const char *s);

   struct idset *idset_decode_ex (const char *s,
                                  ssize_t len,
                                  ssize_t size,
                                  int flags,
                                  idset_error_t *error);

   bool idset_decode_empty (const char *s, ssize_t len);

   int idset_decode_info (const char *s,
                          ssize_t len,
                          size_t *count,
                          unsigned int *maxid,
                          idset_error_t *error);

   int idset_decode_add (struct idset *idset,
                         const char *s,
                         ssize_t len,
                         idset_error_t *error);

   int idset_decode_subtract (struct idset *idset,
                              const char *s,
                              ssize_t len,
                              idset_error_t *error);

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

:func:`idset_decode_ex` creates an idset from a string :var:`s` optionally
truncated at :var:`len` bytes.  If :var:`len` is -1, the full, NUL-terminated
string length is parsed.  The idset is created using the specified :var:`flags`
and :var:`size`.  If :var:`size` is -1, the idset is made to exactly fit the
largest id specified in :var:`s`.  If :var:`size` is 0, a default size is used.

The following functions parse an idset string without creating an idset:

:func:`idset_decode_empty` parses :var:`s` optionally truncated at :var:`len`
and returns true if it is the empty set, or false if it is not the empty set
or cannot be parsed.

:func:`idset_decode_info` parses :var:`s` optionally truncated at :var:`len`
bytes to determine the idset :var:`count` and :var:`maxid`.  Either of the
two output parameters may be set to NULL to suppress assignment.

:func:`idset_decode_add` parses :var:`s` optionally truncated at :var:`len`
bytes and adds members of the resulting set to :var:`idset`.

:func:`idset_decode_subtract` parses :var:`s` optionally truncated at
:var:`len` bytes and removes members of the resulting set from :var:`idset`.


CAVEATS
=======

:func:`idset_decode_ex` without IDSET_FLAGS_AUTOGROW fails when presented
with an empty set because an idset cannot be created with zero slots.
In situations where the set should not grow automatically and the empty
set is valid, it is recommended to represent the empty set with NULL,
and use :func:`idset_decode_empty` to distinguish it from a parse error, e.g.

.. code-block:: c

    struct idset *idset;
    idset_error_t error;

    if (idset_decode_empty (input, -1))
        idset = NULL;
    else if (!(idset = idset_decode_ex (input, -1, -1, 0, &error)))
        // handle parse error


RETURN VALUE
============

:func:`idset_decode_empty` returns a boolean value.

:func:`idset_decode` and :func:`idset_decode_ex` return an idset on success
which must be freed with :man3:`idset_destroy`.  On failure, they return NULL
with :var:`errno` set.  In addition, :func:`idset_decode_ex` sets :var:`error`
on failure, if non-NULL.

:func:`idset_decode_empty` returns a boolean value.

:func:`idset_decode_add` and :func:`idset_decode_subtract` return 0 on success,
or -1 on failure with :var:`errno` and :var:`error` (if non-NULL) set.


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
