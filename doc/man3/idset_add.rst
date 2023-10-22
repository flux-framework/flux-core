============
idset_add(3)
============

.. default-domain:: c

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

:func:`idset_union` creates a new idset that is the union of *a* and *b*.

:func:`idset_difference` creates a new idset that is *a* with the members of
*b* removed.

:func:`idset_intersect` creates a new idset containing only members of *a*
and *b* that are in both sets.

:func:`idset_add` adds the members of *b* to *a*.


:func:`idset_subtract` removes the members of *b* from *a*.

:func:`idset_has_intersection` tests whether *a* and *b* have any members
in common.

:func:`idset_clear_all` removes all members of *x*


RETURN VALUE
============

:func:`idset_union`, :func:`idset_difference`, and :func:`idset_intersect`
return an idset on success which must be freed with :man3:`idset_destroy`.
On error, NULL is returned with errno set.

:func:`idset_add`, :func:`idset_subtract`, and :func:`idset_clear_all`
return 0 on success.  On error, -1 is returned with errno set.

:func:`idset_has_intersection` returns true or false.


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
