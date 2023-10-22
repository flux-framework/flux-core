=====================
flux_shell_aux_set(3)
=====================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   typedef void (*flux_free_f)(void *arg);

   int flux_shell_aux_set (flux_shell_t *shell,
                           const char *name,
                           void *aux,
                           flux_free_f free_fn);

   void * flux_shell_aux_get (flux_shell_t *shell,
                              const char *key);


DESCRIPTION
===========

flux_shell_aux_set() attaches application-specific data to the parent
object. It stores data aux by key name, with optional destructor
destroy. The destructor, if non-NULL, is called when the parent
object is destroyed, or when key is overwritten by a new value. If aux
is NULL, the destructor for a previous value, if any is called, but no
new value is stored. If name is NULL, aux is stored anonymously.

flux_shell_aux_get() retrieves application-specific data by name. If
the data was stored anonymously, it cannot be retrieved.

The implementation (as opposed to the header file) uses the variable
names ``shell``, ``key``, ``val`` and ``free_fn``, which may be more
intuitive.

In most cases the key, value and free function will be non-null.
Several exceptions are supported.

First, if ``key`` and ``val`` are non-NULL but ``free_fn`` is null, the
caller is responsible for memory management associated with the
value.

Second, if ``key`` is NULL but ``val`` and ``free_fun`` are not NULL,
the lifetime of the object is tied to the lifetime of the underlying
aux object; the object will be destroyed during the destruction
of the aux. The value cannot be retrieved.

Third, a non-null ``key`` and a null ``val`` deletes the value previously
associated with the key by calling its previously-associated ``free_fn``,
if the destructor exists.


RETURN VALUE
============

``flux_aux_set()`` returns 0 on success, or -1 on failure, with errno set.

``flux_shell_aux_get()`` returns data on success, or NULL on failure,
with errno set.


ERRORS
======

EINVAL
   | ``shell`` is null; or
   | both ``name`` (aka ``key``) and ``aux`` (aka ``val``) are null; or
   | ``free_fn`` is not null but ``aux`` is; or
   | ``free_fn`` and ``name`` are both null.

ENOMEM
   Out of memory.

ENOENT
   ``flux_aux_get()`` could not find an entry for *key*.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_aux_get`, :man3:`flux_aux_set`
