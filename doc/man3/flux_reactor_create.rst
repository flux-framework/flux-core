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

  void flux_reactor_active_incref (flux_reactor_t *r);

  void flux_reactor_active_decref (flux_reactor_t *r);


DESCRIPTION
===========

``flux_reactor_create()`` creates a flux_reactor_t object which can be used
to monitor for events on file descriptors, ZeroMQ sockets, timers, and
flux_t broker handles.

There is currently only one possible flag for reactor creation:

FLUX_REACTOR_SIGCHLD
   The reactor will internally register a SIGCHLD handler and be capable
   of handling flux child watchers (see :man3:`flux_child_watcher_create`).

For each event source and type that is to be monitored, a flux_watcher_t
object is created using a type-specific create function, and started
with :man3:`flux_watcher_start`.

For each event source and type that is to be monitored, a flux_watcher_t
object is created and associated with a specific reactor using a type-specific
create function, and started with :man3:`flux_watcher_start`. To receive events,
control must be transferred to the reactor event loop by calling
``flux_reactor_run()``.

The full list of flux reactor run flags is as follows:

FLUX_REACTOR_NOWAIT
   Run one reactor loop iteration without blocking.

FLUX_REACTOR_ONCE
   Run one reactor loop iteration, blocking until at least one event is handled.

flux_reactor_run() processes events until one of the following conditions
is met:

-  There are no more active watchers.

-  The ``flux_reactor_stop()`` or ``flux_reactor_stop_error()`` functions
   are called by one of the watchers.

-  Flags include FLUX_REACTOR_NOWAIT and one reactor loop iteration
   has been completed.

-  Flags include FLUX_REACTOR_ONCE, at least one event has been handled,
   and one reactor loop iteration has been completed.

If ``flux_reactor_stop_error()`` is called, this will cause
``flux_reactor_run()`` to return -1 indicating that an error has occurred.
The caller should ensure that a valid error code has been assigned to
:linux:man3:`errno` before calling this function.

``flux_reactor_destroy()`` releases an internal reference taken at
``flux_reactor_create()`` time. Freeing of the underlying resources will
be deferred if there are any remaining watchers associated with the reactor.

``flux_reactor_active_decref()`` and ``flux_reactor_active_incref()`` manipulate
the reactor's internal count of active watchers. Each active watcher takes
a reference count on the reactor, and the reactor returns when this count
reaches zero. It is useful sometimes to have a watcher that can remain
active without preventing the reactor from exiting. To achieve this,
call ``flux_reactor_active_decref()`` after the watcher is started, and
``flux_reactor_active_incref()`` before the watcher is stopped.
Remember that destroying an active reactor internally stops it,
so be sure to stop/incref such a watcher first.


RETURN VALUE
============

``flux_reactor_create()`` returns a flux_reactor_t object on success.
On error, NULL is returned, and errno is set appropriately.

``flux_reactor_run()`` returns the number of active watchers on success.
On failure, it returns -1 with errno set. A failure return is triggered
when the application sets errno and calls ``flux_reactor_stop_error()``.


ERRORS
======

ENOMEM
   Out of memory.


RESOURCES
=========

Flux: http://flux-framework.org

libev: http://software.schmorp.de/pkg/libev.html


SEE ALSO
========

:man3:`flux_fd_watcher_create`, :man3:`flux_handle_watcher_create`,
:man3:`flux_timer_watcher_create`, :man3:`flux_watcher_start`
