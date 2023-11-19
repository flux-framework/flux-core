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

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_shell_aux_set` attaches application-specific data to the parent
object. It stores data :var:`aux` by key :var:`name`, with optional destructor
:var:`destroy`. The destructor, if non-NULL, is called when the parent
object is destroyed, or when :var:`name` is overwritten by a new value. If
:var:`aux` is NULL, the destructor for a previous value, if any is called,
but no new value is stored. If :var:`name` is NULL, :var:`aux` is stored
anonymously.

:func:`flux_shell_aux_get` retrieves application-specific data by name. If
the data was stored anonymously, it cannot be retrieved.

The implementation (as opposed to the header file) uses the variable
names :var:`shell`, :var:`key`, :var:`val` and :var:`free_fn`, which may be
more intuitive.

In most cases the :var:`key`, :var:`value` and :var:`free` function will be
non-null.  Several exceptions are supported.

First, if :var:`key` and :var:`val` are non-NULL but :var:`free_fn` is null,
the caller is responsible for memory management associated with the value.

Second, if :var:`key` is NULL but :var:`val` and :var:`free_fun` are not NULL,
the lifetime of the object is tied to the lifetime of the underlying
aux object; the object will be destroyed during the destruction
of the aux. The value cannot be retrieved.

Third, a non-null :var:`key` and a null :var:`val` deletes the value previously
associated with the key by calling its previously-associated :var:`free_fn`,
if the destructor exists.


RETURN VALUE
============

:func:`flux_aux_set` returns 0 on success, or -1 on failure, with :var:`errno`
set.

:func:`flux_shell_aux_get` returns data on success, or NULL on failure,
with :var:`errno` set.


ERRORS
======

EINVAL
   | :var:`shell` is null; or
   | both :var:`name` (aka :var:`key`) and :var:`aux` (aka :var:`val`) are null; or
   | :var:`free_fn` is not null but :var:`aux` is; or
   | :var:`free_fn` and :var:`name` are both null.

ENOMEM
   Out of memory.

ENOENT
   :func:`flux_aux_get` could not find an entry for :var:`key`.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_aux_get`, :man3:`flux_aux_set`
