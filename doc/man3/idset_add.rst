============
idset_add(3)
============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/idset.h>

   bool idset_equal (const struct idset *a,
                     const struct idset *b);

   struct idset *idset_union (const struct idset *a,
                              const struct idset *b);

   struct idset *idset_difference (const struct idset *a,
                                   const struct idset *b);

   struct idset *idset_intersect (const struct idset *a,
                                  const struct idset *b);

   int idset_add (struct idset *a, const struct idset *b);

   int idset_subtract (struct idset *a, const struct idset *b);

   bool idset_has_intersection (const struct idset *a,
                                const struct idset *b);

   #define idset_clear_all (x) idset_subtract (x, x)

Link with :command:`-lflux-idset`.

DESCRIPTION
===========

Refer to :man3:`idset_create` for a general description of idsets.

:func:`idset_equal` returns true if the two idset objects :var:`a` and
:var:`b` are equal sets, i.e. the sets contain the same set of integers.

:func:`idset_union` creates a new idset that is the union of :var:`a` and
:var:`b`.

:func:`idset_difference` creates a new idset that is :var:`a` with the members
of :var:`b` removed.

:func:`idset_intersect` creates a new idset containing only members of :var:`a`
and :var:`b` that are in both sets.

:func:`idset_add` adds the members of :var:`b` to :var:`a`.

:func:`idset_subtract` removes the members of :var:`b` from :var:`a`.

:func:`idset_has_intersection` tests whether :var:`a` and :var:`b` have any
members in common.

:func:`idset_clear_all` removes all members of :var:`x`.


RETURN VALUE
============

:func:`idset_union`, :func:`idset_difference`, and :func:`idset_intersect`
return an idset on success which must be freed with :man3:`idset_destroy`.
On error, NULL is returned with :var:`errno` set.

:func:`idset_add`, :func:`idset_subtract`, and :func:`idset_clear_all`
return 0 on success.  On error, -1 is returned with :var:`errno` set.

:func:`idset_equal` and :func:`idset_has_intersection` return true or false.


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

:man3:`idset_create`, :man3:`idset_encode`
