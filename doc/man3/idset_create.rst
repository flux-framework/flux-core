===============
idset_create(3)
===============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/idset.h>

   struct idset *idset_create (size_t slots, int flags);

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
                            unsigned int prev);

   unsigned int idset_last (const struct idset *idset);

   size_t idset_count (const struct idset *idset);

   bool idset_equal (const struct idset *set1,
                     const struct idset *set2);

Link with :command:`-lflux-idset`.

DESCRIPTION
===========

An idset is a set of numerically sorted, non-negative integers.
It is internally represented as a van Embde Boas (or vEB) tree.
Functionally it behaves like a bitmap, and has space efficiency
comparable to a bitmap, but performs operations (insert, delete,
lookup, findNext, findPrevious) in O(log(m)) time, where pow (2,m)
is the number of slots in the idset.

:func:`idset_create` creates an idset. :var:`slots` specifies the highest
numbered *id* it can hold, plus one. The size is fixed unless
:var:`flags` specify otherwise (see FLAGS below).

:func:`idset_destroy` destroys an idset.

:func:`idset_copy` copies an idset.

:func:`idset_set` and :func:`idset_clear` set or clear :var:`id`.

:func:`idset_range_set` and :func:`idset_range_clear` set or clear an inclusive
range of ids, from :var:`lo` to :var:`hi`.

:func:`idset_test`` returns true if :var:`id` is set, false if not.

:func:`idset_first` and :func:`idset_next` can be used to iterate over ids
in the set, returning IDSET_INVALID_ID at the end. :func:`idset_last`
returns the last (highest) id, or IDSET_INVALID_ID if the set is
empty.

:func:`idset_count` returns the number of ids in the set.

:func:`idset_equal` returns true if the two idset objects :var:`set1` and
:var:`set2` are equal sets, i.e. the sets contain the same set of integers.


FLAGS
=====

IDSET_FLAG_AUTOGROW
   Valid for :func:`idset_create` only. If set, the idset will grow to
   accommodate any id inserted into it. The internal vEB tree is doubled
   in size until until the new id can be inserted. Resizing is a costly
   operation that requires all ids in the old tree to be inserted into
   the new one.


RETURN VALUE
============

:func:`idset_copy` returns an idset on success which must be freed with
:func:`idset_destroy`. On error, NULL is returned with :var:`errno` set.

:func:`idset_first`, :func:`idset_next`, and :func:`idset_last` return an id,
or IDSET_INVALID_ID if no id is available.

:func:`idset_equal` returns true if :var:`set1` and :var:`set2` are equal sets,
or false if they are not equal, or either argument is *NULL*.

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

:man3:`idset_encode`, :man3:`idset_add`
