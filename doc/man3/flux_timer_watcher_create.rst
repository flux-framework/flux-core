============================
flux_timer_watcher_create(3)
============================


SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   typedef void (*flux_watcher_f)(flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents,
                                  void *arg);

   flux_watcher_t *flux_timer_watcher_create (flux_reactor_t *r,
                                              double after,
                                              double repeat,
                                              flux_watcher_f callback,
                                              void *arg);

   void flux_timer_watcher_reset (flux_watcher_t *w,
                                  double after,
                                  double repeat);


DESCRIPTION
===========

``flux_timer_watcher_create()`` creates a flux_watcher_t object which
monitors for timer events. A timer event occurs when *after* seconds
have elapsed, and optionally again every *repeat* seconds.
When events occur, the user-supplied *callback* is invoked.

If *after* is 0., the flux_watcher_t will be immediately ready
when the reactor is started. If *repeat* is 0., the flux_watcher_t
will automatically be stopped when *after* seconds have elapsed.

Note that *after* is internally referenced to reactor time, which is
only updated when the reactor is run/created, and therefore
can be out of date. Use :man3:`flux_reactor_now_update` to manually
update reactor time before creating timer watchers in such cases.
Refer to "The special problem of time updates" in the libev manual
for more information.

To restart a timer that has been automatically stopped, you must reset
the *after* and *repeat* values with ``flux_timer_watcher_reset()`` before
calling ``flux_watcher_start()``.

The callback *revents* argument should be ignored.

Note: the Flux reactor is based on libev. For additional information
on the behavior of timers, refer to the libev documentation on ``ev_timer``.


RETURN VALUE
============

``flux_timer_watcher_create()`` returns a flux_watcher_t object on success.
On error, NULL is returned, and errno is set appropriately.


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

:man3:`flux_watcher_start`, :man3:`flux_reactor_run`, :man3:`flux_reactor_now`
