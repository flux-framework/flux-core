============================
flux_child_watcher_create(3)
============================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   typedef void (*flux_watcher_f)(flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents,
                                  void *arg);

   flux_watcher_t *flux_child_watcher_create (flux_reactor_t *r,
                                              int pid,
                                              bool trace,
                                              flux_watcher_f cb,
                                              void *arg);

   int flux_child_watcher_get_rpid (flux_watcher_t *w);

   int flux_child_watcher_get_rstatus (flux_watcher_t *w);


DESCRIPTION
===========

:func:`flux_child_watcher_create` creates a reactor watcher that
monitors state transitions of child processes. If :var:`trace` is false,
only child termination will trigger an event; otherwise, stop and start
events may be generated.

The callback :var:`revents` argument should be ignored.

The process id that had a transition may be obtained by calling
:func:`flux_child_watcher_get_rpid`.

The status value returned by :linux:man2:`waitpid` may be obtained by calling
:func:`flux_child_watcher_get_rstatus`.

Only a Flux reactor created with the FLUX_REACTOR_SIGCHLD flag can
be used with child watchers, as the reactor must register a SIGCHLD
signal watcher before any processes are spawned. Only one reactor instance
per program may be created with this capability.


RETURN VALUE
============

:func:`flux_child_watcher_create` returns a :type:`flux_watcher_t` object on
success.  On error, NULL is returned, and :var:`errno` is set appropriately.


ERRORS
======

ENOMEM
   Out of memory.

EINVAL
   Reactor was not created with FLUX_REACTOR_SIGCHLD.


RESOURCES
=========

Flux: http://flux-framework.org

libev: http://software.schmorp.de/pkg/libev.html


SEE ALSO
========

:man3:`flux_watcher_start`, :man3:`flux_reactor_run`
