===========================
flux_idle_watcher_create(3)
===========================


SYNOPSIS
========

::

   #include <flux/core.h>

::

   typedef void (*flux_watcher_f)(flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents, void *arg);

::

   flux_watcher_t *flux_prepare_watcher_create (flux_reactor_t *r,
                                                flux_watcher_f callback,
                                                void *arg);

::

   flux_watcher_t *flux_check_watcher_create (flux_reactor_t *r,
                                              flux_watcher_f callback,
                                              void *arg);

::

   flux_watcher_t *flux_idle_watcher_create (flux_reactor_t *r,
                                             flux_watcher_f callback,
                                             void *arg);


DESCRIPTION
===========

``flux_prepare_watcher_create()``, ``flux_check_watcher_create()``, and
``flux_idle_watcher_create()`` create specialized reactor watchers with
the following properties:

The prepare watcher is called by the reactor loop immediately before
blocking, while the check watcher is called by the reactor loop
immediately after blocking.

The idle watcher is always run when no other events are pending,
excluding other idle watchers, prepare and check watchers.
While it is active, the reactor loop does not block waiting for
new events.

The callback *revents* argument should be ignored.

Note: the Flux reactor is based on libev. For additional information
on the behavior of these watchers, refer to the libev documentation on
``ev_idle``, ``ev_prepare``, and ``ev_check``.


RETURN VALUE
============

These functions return a flux_watcher_t object on success.
On error, NULL is returned, and errno is set appropriately.


ERRORS
======

ENOMEM
   Out of memory.


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

flux_watcher_start(3), flux_reactor_start(3)

`libev home page <http://software.schmorp.de/pkg/libev.html>`__
