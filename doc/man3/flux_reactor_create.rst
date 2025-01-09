======================
flux_reactor_create(3)
======================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  flux_reactor_t *flux_reactor_create (int flags);

  void flux_reactor_destroy (flux_reactor_t *r);

  int flux_reactor_run (flux_reactor_t *r, int flags);

  void flux_reactor_stop (flux_reactor_t *r);

  void flux_reactor_stop_error (flux_reactor_t *r);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_reactor_create` creates a :type:`flux_reactor_t` object which can
be used to monitor for events on file descriptors, timers, and
:type:`flux_t` broker handles.  :var:`flags` should be set to zero.

For each event source and type that is to be monitored, a :type:`flux_watcher_t`
object is created using a type-specific create function, and started
with :man3:`flux_watcher_start`.

For each event source and type that is to be monitored, a
:type:`flux_watcher_t` object is created and associated with a specific
reactor using a type-specific create function, and started with
:man3:`flux_watcher_start`. To receive events, control must be transferred
to the reactor event loop by calling :func:`flux_reactor_run`.

The full list of flux reactor run flags is as follows:

FLUX_REACTOR_NOWAIT
   Run one reactor loop iteration without blocking.

FLUX_REACTOR_ONCE
   Run one reactor loop iteration, blocking until at least one event is handled.

:func:`flux_reactor_run` processes events until one of the following conditions
is met:

-  There are no more active watchers.

-  The :func:`flux_reactor_stop` or :func:`flux_reactor_stop_error` functions
   are called by one of the watchers.

-  Flags include FLUX_REACTOR_NOWAIT and one reactor loop iteration
   has been completed.

-  Flags include FLUX_REACTOR_ONCE, at least one event has been handled,
   and one reactor loop iteration has been completed.

If :func:`flux_reactor_stop_error` is called, this will cause
:func:`flux_reactor_run` to return -1 indicating that an error has occurred.
The caller should ensure that a valid error code has been assigned to
:linux:man3:`errno` before calling this function.

:func:`flux_reactor_destroy` releases an internal reference taken at
:func:`flux_reactor_create` time. Freeing of the underlying resources will
be deferred if there are any remaining watchers associated with the reactor.


RETURN VALUE
============

:func:`flux_reactor_create` returns a :type:`flux_reactor_t` object on success.
On error, NULL is returned, and :var:`errno` is set appropriately.

:func:`flux_reactor_run` returns the number of active watchers on success.
On failure, it returns -1 with :var:`errno` set. A failure return is triggered
when the application sets :var:`errno` and calls
:func:`flux_reactor_stop_error`.


ERRORS
======

ENOMEM
   Out of memory.


RESOURCES
=========

.. include:: common/resources.rst

libev: http://software.schmorp.de/pkg/libev.html


SEE ALSO
========

:man3:`flux_fd_watcher_create`, :man3:`flux_handle_watcher_create`,
:man3:`flux_timer_watcher_create`, :man3:`flux_watcher_start`
