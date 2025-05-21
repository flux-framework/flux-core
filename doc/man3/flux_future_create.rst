=====================
flux_future_create(3)
=====================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   typedef void (*flux_future_init_f)(flux_future_t *f,
                                      flux_reactor_t *r,
                                      void *arg);

   flux_future_t *flux_future_create (flux_future_init_f cb, void *arg);

   void flux_future_fulfill (flux_future_t *f,
                             void *result,
                             flux_free_f free_fn);

   void flux_future_fulfill_error (flux_future_t *f,
                                   int errnum,
                                   const char *errstr);

   void flux_future_fulfill_with (flux_future_t *f, flux_future_t *p);

   void flux_future_fatal_error (flux_future_t *f,
                                 int errnum,
                                 const char *errstr);

   void *flux_future_aux_get (flux_future_t *f, const char *name);

   int flux_future_aux_set (flux_future_t *f,
                            const char *name,
                            void *aux,
                            flux_free_f destroy);

   void flux_future_set_reactor (flux_future_t *f, flux_reactor_t *r);

   flux_reactor_t *flux_future_get_reactor (flux_future_t *f);

   void flux_future_set_flux (flux_future_t *f, flux_t *h);

   flux_t *flux_future_get_flux (flux_future_t *f);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

See :man3:`flux_future_get` for general functions that operate on futures.
This page covers functions primarily used when building classes that
return futures.

A Flux future represents some activity that may be completed with reactor
watchers and/or message handlers. It is intended to be returned by other
classes as a handle for synchronization and a container for results.
This page describes the future interfaces used by such classes.

A class that returns a future usually provides a creation function that
internally calls :func:`flux_future_create`, and may provide functions to
access class-specific result(s), that internally call :man3:`flux_future_get`.
The create function internally registers a :type:`flux_future_init_f`
function that is called lazily by the future implementation to perform
class-specific reactor setup, such as installing watchers and message
handlers.

:func:`flux_future_create` creates a future and registers the
class-specific initialization callback :var:`cb`, and an opaque argument
:var:`arg` that will be passed to :var:`cb`. The purpose of the initialization
callback is to set up class-specific watchers on a reactor obtained
with :func:`flux_future_get_reactor`, or message handlers on a :type:`flux_t`
handle obtained with :func:`flux_future_get_flux`, or both.
:func:`flux_future_get_reactor` and :func:`flux_future_get_flux` return
different results depending on whether the initialization callback is
triggered by a user calling :man3:`flux_future_then` or
:man3:`flux_future_wait_for`. The function may be triggered in one or
both contexts, at most once for each. The watchers or message
handlers must eventually call :func:`flux_future_fulfill`,
:func:`flux_future_fulfill_error`, or :func:`flux_future_fatal_error` to
fulfill the future. See REACTOR CONTEXTS below for more information.

:func:`flux_future_fulfill` fulfills the future, assigning an opaque
:var:`result` value with optional destructor :var:`free_fn` to the future.
A NULL :var:`result` is valid and also fulfills the future. The :var:`result`
is contained within the future and can be accessed with :man3:`flux_future_get`
as needed until the future is destroyed.

:func:`flux_future_fulfill_error` fulfills the future, assigning an
:var:`errnum` value and an optional error string. After the future is
fulfilled with an error, :man3:`flux_future_get` will return -1 with
:var:`errno` set to :var:`errnum`.

:func:`flux_future_fulfill_with` fulfills the target future :var:`f` using a
fulfilled future :var:`p`. This function copies the pending result or error
from :var:`p` into :var:`f`, and adds read-only access to the :var:`aux` items
for :var:`p` from :var:`f`. This ensures that any ``get`` method which requires
:var:`aux` items for :var:`p` will work with :var:`f`. This function takes a
reference to the source future :var:`p`, so it safe to call
:man3:`flux_future_destroy` on :var:`p` after this call.
:func:`flux_future_fulfill_with` returns -1 on error with :var:`errno`
set on failure.

:func:`flux_future_fulfill`, :func:`flux_future_fulfill_with`, and
:func:`flux_future_fulfill_error` can be called multiple times to queue
multiple results or errors. When callers access future results via
:man3:`flux_future_get`, results or errors will be returned in FIFO order.
It is an error to call :func:`flux_future_fulfill_with` multiple times on
the same target future :var:`f` with a different source future :var:`p`.

:func:`flux_future_fatal_error` fulfills the future, assigning an :var:`errnum`
value and an optional error string. Unlike
:func:`flux_future_fulfill_error` this fulfillment can only be called once
and takes precedence over all other fulfillments. It is used for
catastrophic error paths in future fulfillment.

:func:`flux_future_aux_set` attaches application-specific data
to the parent object :var:`f`. It stores data :var:`aux` by key :var:`name`,
with optional destructor *destroy*. The destructor, if non-NULL,
is called when the parent object is destroyed, or when
:var:`key` is overwritten by a new value. If :var:`aux` is NULL,
the destructor for a previous value, if any is called,
but no new value is stored. If :var:`name` is NULL,
:var:`aux` is stored anonymously.

:func:`flux_future_aux_get` retrieves application-specific data
by :var:`name`. If the data was stored anonymously, it
cannot be retrieved.

Names beginning with "flux::" are reserved for internal use.

:func:`flux_future_set_reactor` may be used to associate a Flux reactor
with a future. The reactor (or a temporary one, depending on the context)
may be retrieved using :func:`flux_future_get_reactor`.

:func:`flux_future_set_flux` may be used to associate a Flux broker handle
with a future. The handle (or a clone associated with a temporary reactor,
depending on the context) may be retrieved using :func:`flux_future_get_flux`.

Futures may "contain" other futures, to arbitrary depth. That is, an
init callback may create futures and use their continuations to fulfill
the containing future in the same manner as reactor watchers and message
handlers.


REACTOR CONTEXTS
================

Internally, a future can operate in two reactor contexts. The initialization
callback may be called in either or both contexts, depending on which
synchronization functions are called by the user.
:func:`flux_future_get_reactor` and :func:`flux_future_get_flux` return a
result that depends on which context they are called from.

When the user calls :man3:`flux_future_then`, this triggers a call to the
initialization callback. The callback would typically call
:func:`flux_future_get_reactor` and/or :func:`flux_future_get_flux()` to obtain
the reactor or :type:`flux_t` handle to be used to set up watchers or message
handlers.  In this context, the reactor or :type:`flux_t` handle are exactly
the ones passed to :func:`flux_future_set_reactor` and
:func:`flux_future_set_flux`.

When the user calls :man3:`flux_future_wait_for`, this triggers the creation
of a temporary reactor, then a call to the initialization callback.
The temporary reactor allows these functions to wait *only* for the future's
events, without allowing unrelated watchers registered in the main reactor
to run, which might complicate the application's control flow. In this
context, :func:`flux_future_get_reactor` returns the temporary reactor, not
the one passed in with :func:`flux_future_set_reactor`.
:func:`flux_future_get_flux` returns a temporary :type:`flux_t` handle cloned
from the one passed to :func:`flux_future_set_flux`, and associated with the
temporary reactor.
After the internal reactor returns, any messages unmatched by the dispatcher
on the cloned handle are requeued in the main :type:`flux_t` handle with
:func:`flux_dispatch_requeue`.

Since the init callback may be made in either reactor context (at most once
each), and is unaware of which context that is, it should take care when
managing any context-specific state not to overwrite the state from a prior
call. The ability to attach objects with destructors anonymously to the future
with :func:`flux_future_aux_set` may be useful for managing the life cycle
of reactor watchers and message handlers created by init callbacks.


RETURN VALUE
============

:func:`flux_future_create` returns a future on success. On error, NULL is
returned and :var:`errno` is set appropriately.

:func:`flux_future_aux_set` returns zero on success. On error, -1 is
returned and :var:`errno` is set appropriately.

:func:`flux_future_aux_get` returns the requested object on success. On
error, NULL is returned and :var:`errno` is set appropriately.

:func:`flux_future_get_flux` returns a :type:`flux_t` handle on success.
On error, NULL is returned and :var:`errno` is set appropriately.

:func:`flux_future_get_reactor` returns a :type:`flux_reactor_t` on success.
On error, NULL is returned and :var:`errno` is set appropriately.

:func:`flux_future_fulfill_with` returns zero on success. On error, -1 is
returned with :var:`errno` set to EINVAL if either :var:`f` or :var:`p` is
NULL, or :var:`f` and :var:`p` are the same, EAGAIN if the future :var:`p` is
not ready, or EEXIST if the function is called multiple times with different
:var:`p`.


ERRORS
======

ENOMEM
   Out of memory.

EINVAL
   Invalid argument.

ENOENT
   The requested object is not found.

EAGAIN
   The requested operation is not ready. For :func:`flux_future_fulfill_with`,
   the target future :var:`p` is not fulfilled.

EEXIST
   :func:`flux_future_fulfill_with` was called multiple times with a different
   target future :var:`p`.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_future_get`, :man3:`flux_clone`
