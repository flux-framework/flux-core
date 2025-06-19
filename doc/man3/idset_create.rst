===============
idset_create(3)
===============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/idset.h>

   struct idset *idset_create (size_t size, int flags);

   void idset_destroy (struct idset *idset);

   struct idset *idset_copy (const struct idset *idset);

   int idset_set (struct idset *idset, unsigned int id);

   int idset_range_set (struct idset *idset,
                        unsigned int lo,
                        unsigned int hi);

   int idset_clear (struct idset *idset, unsigned int id);

   int idset_range_clear (struct idset *idset,
                          unsigned int lo,
                          unsigned int hi);

   bool idset_test (const struct idset *idset, unsigned int id);

   unsigned int idset_first (const struct idset *idset);

   unsigned int idset_next (const struct idset *idset,
                            unsigned int id);

   unsigned int idset_last (const struct idset *idset);

   unsigned int idset_prev (const struct idset *idset,
                            unsigned int id);

   size_t idset_count (const struct idset *idset);

   bool idset_empty (const struct idset *idset);

   size_t idset_universe_size (const struct idset *idset);

Link with :command:`-lflux-idset`.

DESCRIPTION
===========

An idset is a set of numerically sorted, non-negative integers.
It is internally represented as a van Embde Boas (or vEB) tree.
Functionally it behaves like a bitmap, and has space efficiency
comparable to a bitmap, but performs *test*, *set*, *clear*, *next*,
and *prev* operations in :math:`O(log(m))` time (where :math:`2^m` is the
universe size); and performs *first* and *last* operations in constant time.

:func:`idset_create` creates an idset. :var:`size` specifies the universe
size, which is the maximum *id* it can hold, plus one. The universe size is
fixed unless :var:`flags` specify otherwise (see FLAGS below).

:func:`idset_destroy` destroys an idset.

:func:`idset_copy` copies an idset.

:func:`idset_set` and :func:`idset_clear` set or clear :var:`id`.

:func:`idset_range_set` and :func:`idset_range_clear` set or clear an inclusive
range of ids, from :var:`lo` to :var:`hi`.

:func:`idset_test` returns true if :var:`id` is set, false if not.

:func:`idset_first` and :func:`idset_next` can be used to iterate forward
over ids in the set, returning IDSET_INVALID_ID at the end.

:func:`idset_last` and :func:`idset_prev` can be used to iterate backward
over ids in the set, returning IDSET_INVALID_ID at the end.

:func:`idset_count` returns the number of ids in the set.  A running count
is kept so this function runs in constant time.

:func:`idset_empty` returns true if the set is empty.  This function runs
in constant time.

:func:`idset_universe_size` returns the current set universe size.
This is normally the size specified at creation, or a multiple of it if
IDSET_FLAG_AUTOGROW was specified.

FLAGS
=====

The following flags are valid for :func:`idset_create`:

IDSET_FLAG_AUTOGROW
   The idset will grow to accommodate any id that is the target of a set, or
   if IDSET_FLAG_INITFULL is set, a clear operation.  The universe size is
   doubled until until the new id can be accessed.  Resizing is a costly
   operation that requires all ids in the old tree to be inserted into the
   new one.

IDSET_FLAG_INITFULL
   The idset is created full instead of empty.  If specified with
   IDSET_FLAG_AUTOGROW, new portions that are added are also filled.

IDSET_FLAG_ALLOC_RR
   Change :func:`idset_alloc` to begin searching for an id after the
   most recently allocated one, rather than taking the first available.

IDSET_FLAG_COUNT_LAZY
   The running count is not maintained and :func:`idset_count` uses a slower
   iteration method.  Not maintaining the count makes set/clear operations
   slightly faster, an acceptable trade-off for some use cases.  This flag does
   not affect :func:`idset_empty`.


RETURN VALUE
============

:func:`idset_create` and :func:`idset_copy` return an idset on success which
must be freed with :func:`idset_destroy`. On error, NULL is returned with
:var:`errno` set.

:func:`idset_first`, :func:`idset_next`, :func:`idset_prev`, and
:func:`idset_last` return an id, or IDSET_INVALID_ID if no id is available.

:func:`idset_count` and :func:`idset_universe_size` return 0 if the argument
is invalid.

:func:`idset_empty` returns true for the empty set or invalid arguments.

Other functions return 0 on success, or -1 on error with :var:`errno` set.


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

:man3:`idset_encode`, :man3:`idset_add`, :man3:`idset_alloc`
