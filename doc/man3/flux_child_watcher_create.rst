============================
flux_child_watcher_create(3)
============================


SYNOPSIS
========

::

   #include <flux/core.h>

::

   typedef void (*flux_watcher_f)(flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents, void *arg);

::

   flux_watcher_t *flux_child_watcher_create (flux_reactor_t *r,
                                              int pid, bool trace,
                                              flux_watcher_f cb, void *arg);

::

   int flux_child_watcher_get_rpid (flux_watcher_t *w);

::

   int flux_child_watcher_get_rstatus (flux_watcher_t *w);


DESCRIPTION
===========

``flux_child_watcher_create()`` creates a reactor watcher that
monitors state transitions of child processes. If *trace* is false,
only child termination will trigger an event; otherwise, stop and start
events may be generated.

The callback *revents* argument should be ignored.

The process id that had a transition may be obtained by calling
``flux_child_watcher_get_rpid()``.

The status value returned by waitpid(2) may be obtained by calling
``flux_child_watcher_get_rstatus()``.

Only a Flux reactor created with the FLUX_REACTOR_SIGCHLD flag can
be used with child watchers, as the reactor must register a SIGCHLD
signal watcher before any processes are spawned. Only one reactor instance
per program may be created with this capability.


RETURN VALUE
============

flux_child_watcher_create() returns a flux_watcher_t object on success.
On error, NULL is returned, and errno is set appropriately.


ERRORS
======

ENOMEM
   Out of memory.

EINVAL
   Reactor was not created with FLUX_REACTOR_SIGCHLD.


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

flux_watcher_start(3), flux_reactor_start(3)

`libev home page <http://software.schmorp.de/pkg/libev.html>`__
