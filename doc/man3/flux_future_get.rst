==================
flux_future_get(3)
==================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   typedef void (*flux_continuation_f)(flux_future_t *f, void *arg);

   int flux_future_get (flux_future_t *f, const void **result);

   int flux_future_then (flux_future_t *f,
                         double timeout,
                         flux_continuation_f cb,
                         void *arg);

   int flux_future_wait_for (flux_future_t *f, double timeout);

   void flux_future_reset (flux_future_t *f);

   void flux_future_destroy (flux_future_t *f);

   bool flux_future_has_error (flux_future_t *f);

   const char *flux_future_error_string (flux_future_t *f);

Link with :command:`-lflux-core`.

OVERVIEW
========

A Flux future represents some activity that may be completed with reactor
watchers and/or message handlers. It is both a handle for synchronization
and a container for the result. A Flux future is said to be "fulfilled"
when a result is available in the future container, or a fatal error has
occurred. Flux futures were inspired by similar constructs in other
programming environments mentioned in RESOURCES, but are not a faithful
implementation of any particular one.

Generally other Flux classes return futures, and may provide class-specific
access function for results. The functions described in this page can be
used to access, synchronize, and destroy futures returned from any such class.
Authors of classes that return futures are referred to :man3:`flux_future_create`.


DESCRIPTION
===========

:func:`flux_future_get` accesses the result of a fulfilled future. If the
future is not yet fulfilled, it calls :func:`flux_future_wait_for` internally
with a negative :var:`timeout`, causing it to block until the future is
fulfilled.  A pointer to the result is assigned to :var:`result` (caller must
NOT free), or -1 is returned if the future was fulfilled with an error.

:func:`flux_future_then` sets up a continuation callback :var:`cb` that is
called with opaque argument :var:`arg` once the future is fulfilled. The
continuation will normally use :func:`flux_future_get` or a class-specific
access function to obtain the result from the future container without
blocking. The continuation may call :func:`flux_future_destroy` or
:func:`flux_future_reset`.
If :var:`timeout` is non-negative, the future must be fulfilled within the
specified amount of time or the timeout fulfills it with an error (:var:`errno`
set to ETIMEDOUT).

:func:`flux_future_wait_for` blocks until the future is fulfilled, or
:var:`timeout` (if non-negative) expires. This function may be called multiple
times, with different values for :var:`timeout`. If the timeout expires before
the future is fulfilled, an error is returned (:var:`errno` set to ETIMEDOUT)
but the future remains unfulfilled. If :var:`timeout` is zero, function times
out immediately if the future has not already been fulfilled.

:func:`flux_future_reset` unfulfills a future, invalidating any result stored
in the container, and preparing it to be fulfilled once again. If a
continuation was registered, it remains in effect for the next fulfillment.
If a timeout was specified when the continuation was registered, it is
restarted.

:func:`flux_future_destroy` destroys a future, including any result contained
within.

:func:`flux_future_has_error` tests if an error exists in the future or not.
It can be useful for determining if an error exists in a future or in
other parts of code that may wrap around a future. It is commonly
called before calling :func:`flux_future_error_string`.

:func:`flux_future_error_string` returns the error string stored in a
future. If the future was fulfilled with an optional error string,
:func:`flux_future_error_string` will return that string. Otherwise, it
will return the string associated with the error number set in a
future. If the future is a NULL pointer, not fulfilled, or fulfilled
with a non-error, NULL is returned.


RETURN VALUE
============

:func:`flux_future_then`, :func:`flux_future_get`, and
:func:`flux_future_wait_for` return zero on success. On error, -1 is returned,
and :var:`errno` is set appropriately.


ERRORS
======

ENOMEM
   Out of memory.

EINVAL
   Invalid argument.

ETIMEDOUT
   A timeout passed to :func:`flux_future_wait_for` expired before the future
   was fulfilled.

EDEADLOCK (or EDEADLK on BSD systems)
   :func:`flux_future_wait_for` would likely deadlock due to an
   improperly initialized future.

RESOURCES
=========

.. include:: common/resources.rst

C++ std::future: http://en.cppreference.com/w/cpp/thread/future

Java ``util.concurrent.Future``: https://docs.oracle.com/javase/7/docs/api/java/util/concurrent/Future.html

Python3 concurrent.futures: https://docs.python.org/3/library/concurrent.futures.html


SEE ALSO
========

:man3:`flux_future_create`
