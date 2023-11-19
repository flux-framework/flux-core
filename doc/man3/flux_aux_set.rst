===============
flux_aux_set(3)
===============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   typedef void (*flux_free_f)(void *arg);

   void *flux_aux_get (flux_t *h, const char *name);

   int flux_aux_set (flux_t *h,
                     const char *name,
                     void *aux,
                     flux_free_f destroy);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_aux_set` attaches application-specific data
to the parent object :var:`h`. It stores data :var:`aux` by key :var:`name`,
with optional destructor :var:`destroy`. The destructor, if non-NULL,
is called when the parent object is destroyed, or when
:var:`key` is overwritten by a new value. If :var:`aux` is NULL,
the destructor for a previous value, if any is called,
but no new value is stored. If :var:`name` is NULL,
:var:`aux` is stored anonymously.

:func:`flux_aux_get` retrieves application-specific data
by :var:`name`. If the data was stored anonymously, it
cannot be retrieved.  Note that :func:`flux_aux_get` does not scale to a
large number of items, and flux module handles may persist for a long
time.

Names beginning with "flux::" are reserved for internal use.


RETURN VALUE
============

:func:`flux_aux_get` returns data on success, or NULL on failure,
with :var:`errno` set.

:func:`flux_aux_set` returns 0 on success, or -1 on failure, with errno set.


ERRORS
======

EINVAL
   Some arguments were invalid.

ENOMEM
   Out of memory.

ENOENT
   :func:`flux_aux_get` could not find an entry for *key*.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_open`
