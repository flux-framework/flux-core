============
idset_add(3)
============


SYNOPSIS
========

.. code-block:: c

   #include <flux/idset.h>

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


USAGE
=====

cc [flags] files -lflux-idset [libraries]


DESCRIPTION
===========

Refer to :man3:`idset_create` for a general description of idsets.

``idset_union()`` creates a new idset that is the union of *a* and *b*.

``idset_difference()`` creates a new idset that is *a* with the members of
*b* removed.

``idset_intersect()`` creates a new idset containing only members of *a*
and *b* that are in both sets.

``idset_add()`` adds the members of *b* to *a*.


``idset_subtract()`` removes the members of *b* from *a*.

``idset_has_intersection()`` tests whether *a* and *b* have any members
in common.

``idset_clear_all()`` removes all members of *x*


RETURN VALUE
============

``idset_union()``, ``idset_difference()``, and ``idset_intersect()`` return an
idset on success which must be freed with ``idset_destroy()``. On error,
NULL is returned with errno set.

``idset_add()``, ``idset_subtract()``, and ``idset_clear_all()``  return 0
on success.  On error, -1 is returned with errno set.

``idset_has_intersection()`` returns true or false.


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

:man3:`idset_create`, :man3:`idset_encode`
