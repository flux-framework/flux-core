==============
idset_alloc(3)
==============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/idset.h>

   int idset_alloc (struct idset *idset, unsigned int *val);

   void idset_free (struct idset *idset, unsigned int val);

   int idset_free_check (struct idset *idset, unsigned int val);

Link with :command:`-lflux-idset`.


DESCRIPTION
===========

Refer to :man3:`idset_create` for a general description of idsets.

These functions are useful when using an idset as an integer allocator.
The idset must have been created with IDSET_FLAG_INITFULL.

.. note::
  Unallocated is defined as "in the set" so that allocation can use the
  constant-time *first* operation to find the first available id.  Defining
  unallocated as "not in the set" would mean that iteration would
  be required to find the next available id.  This advantage is lost when
  the set is created with IDSET_FLAG_ALLOC_RR.

:func:`idset_alloc` takes the first available id out of the set.
This is implemented as :func:`idset_first` and :func:`idset_clear` internally.
If the set was created with IDSET_FLAG_ALLOC_RR, it takes the *next*
available id after the most recently allocated one, using :func:`idset_next`.
If there are no more ids available and the set was created with
IDSET_FLAG_AUTOGROW, the set is expanded in order to fulfill the request.

:func:`idset_free` puts an id back in the set.  This is implemented
as :func:`idset_set` internally.

:func:`idset_free_check` is identical to the above, except it fails
if the id is already in the set.  This is implemented as :func:`idset_test`
and :func:`idset_set` internally.

RETURN VALUE
============

:func:`idset_alloc` and :func:`idset_free_check` return 0 on success, or
-1 on error with :var:`errno` set.


ERRORS
======

EINVAL
   One or more arguments were invalid.

ENOMEM
   Out of memory.

EEXIST
   :func:`idset_free_check` was called on an id that is already in the
   idset.


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_22`


SEE ALSO
========

:man3:`idset_create`, :man3:`idset_encode`, :man3:`idset_add`
