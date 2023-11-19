=======================
flux_future_and_then(3)
=======================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   flux_future_t *flux_future_and_then (flux_future_t *f,
                                        flux_continuation_f cb,
                                        void *arg);

   flux_future_t *flux_future_or_then (flux_future_t *f,
                                       flux_continuation_f cb,
                                       void *arg);

   int flux_future_continue (flux_future_t *prev,
                             flux_future_t *f);

   void flux_future_continue_error (flux_future_t *prev,
                                    int errnum,
                                    const char *errstr);

   int flux_future_fulfill_next (flux_future_t *f,
                                 void *result,
                                 flux_free_f free_fn);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

See :man3:`flux_future_get` for general functions that operate on futures,
and :man3:`flux_future_create` for a description of the :type:`flux_future_t`
base type. This page covers functions for the sequential composition of
futures, i.e. chains of dependent futures.

:func:`flux_future_and_then` is similar to :man3:`flux_future_then`, but
returns a future that may later be "continued" from the continuation
callback :var:`cb`. The provided continuation callback :var:`cb` is only
executed when the future argument :var:`f` is fulfilled successfully. On
error, the error from :var:`f` is automatically propagated to the "next"
future in the chain (returned by the function).

:func:`flux_future_and_then` is useful when a series of asynchronous
operations, each returning a :type:`flux_future_t`, depend on the result
of a previous operation. That is, :func:`flux_future_and_then` returns a
placeholder future for an eventual future that can't be created until
the continuation :var:`cb` is run. The returned future can then be
used as a synchronization handle or even passed to another
:func:`flux_future_and_then` in the chain. By default, the next future
in the chain will be fulfilled immediately using the result of the
previous future after return from the callback :var:`cb`. Most callbacks,
however, should use either :func:`flux_future_continue` or
:func:`flux_future_continue_error` to pass an intermediate future
to use in fulfillment of the next future in the chain.

:func:`flux_future_or_then` is like :func:`flux_future_and_then`, except
the continuation callback :var:`cb` is run when the future :var:`f` is fulfilled
with an error. This function is useful for recovery or other error
handling (other than the default behavior of propagating an error
down the chain to the final result). The :func:`flux_future_or_then`
callback offers a chance to successfully fulfill the "next" future
in the chain, even when the "previous" future was fulfilled with
an error.

As with :func:`flux_future_and_then` the continuation
:var:`cb` function for :func:`flux_future_or_then` should call
:func:`flux_future_continue` or :func:`flux_future_continue_error`, or
the result of the previous future will be propagated immediately
to the next future in the chain.

:func:`flux_future_continue` continues the next future embedded in :var:`prev`
(created by :func:`flux_future_and_then` or :func:`flux_future_or_then`) with
the eventual result of the provided future :var:`f`. This allows a future
that was not created until the context of the callback to continue
a sequential chain of futures created earlier. After the call to
:func:`flux_future_continue` completes, the future :var:`prev` may safely be
destroyed. :func:`flux_future_continue` may be called with :var:`f` equal
to ``NULL`` if the caller desires the next future in the chain to
**not** be fulfilled, in order to disable the automatic fulfillment
that normally occurs for non-continued futures after the callback
completes.

:func:`flux_future_continue_error` is like :func:`flux_future_continue`
but immediately fulfills the next future in the chain with an error and
an optional error string. Once :func:`flux_future_continue_error`
completes, the future :var:`prev` may safely be destroyed.

:func:`flux_future_fulfill_next` is like :man3:`flux_future_fulfill`, but
fulfills the next future in the chain instead of the current future (which
is presumably already fulfilled). This call is useful when a chained future
is being used for post-processing a result from intermediate future-based
calls, as it allows the next future to be fulfilled with a custom result,
instead of with the value of another future as in
:func:`flux_future_continue`.


RETURN VALUE
============

:func:`flux_future_and_then` and :func:`flux_future_or_then` return a
:type:`flux_future_t` on success, or NULL on error. If both functions are
called on the same future, the returned :type:`flux_future_t` from each will
be the same object.

:func:`flux_future_continue` returns 0 on success, or -1 on error with errno
set.

:func:`flux_future_fulfill_next` returns 0 on success, or -1 with errno set
to ``EINVAL`` if the target future does not have a next future to fulfill.


ERRORS
======

ENOMEM
   Out of memory.

EINVAL
   Invalid argument.

ENOENT
   The requested object is not found.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_future_get`, :man3:`flux_future_create`
