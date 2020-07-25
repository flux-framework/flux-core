===============
flux_aux_set(3)
===============


SYNOPSIS
========

::

   #include <flux/core.h>

::

   typedef void (*flux_free_f)(void *arg);

::

   void *flux_aux_get (flux_t *h, const char *name);

::

   int flux_aux_set (flux_t *h, const char *name,
                     void *aux, flux_free_f destroy);


DESCRIPTION
===========

``flux_aux_set()`` attaches application-specific data
to the parent object *h*. It stores data *aux* by key *name*,
with optional destructor *destroy*. The destructor, if non-NULL,
is called when the parent object is destroyed, or when
*key* is overwritten by a new value. If *aux* is NULL,
the destructor for a previous value, if any is called,
but no new value is stored. If *name* is NULL,
*aux* is stored anonymously.

``flux_aux_get()`` retrieves application-specific data
by *name*. If the data was stored anonymously, it
cannot be retrieved.

Names beginning with "flux::" are reserved for internal use.


RETURN VALUE
============

``flux_aux_get()`` returns data on success, or NULL on failure, with errno set.

``flux_aux_set()`` returns 0 on success, or -1 on failure, with errno set.


ERRORS
======

EINVAL
   Some arguments were invalid.

ENOMEM
   Out of memory.

ENOENT
   ``flux_aux_get()`` could not find an entry for *key*.


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

flux_open(3)
