============================
flux_timer_watcher_create(3)
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

   flux_watcher_t *flux_timer_watcher_create (flux_reactor_t *r,
                                              double after,
                                              double repeat,
                                              flux_watcher_f callback,
                                              void *arg);

   void flux_timer_watcher_reset (flux_watcher_t *w,
                                  double after,
                                  double repeat);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_timer_watcher_create` creates a :type:`flux_watcher_t` object which
monitors for timer events. A timer event occurs when :var:`after` seconds
have elapsed, and optionally again every :var:`repeat` seconds.
When events occur, the user-supplied :var:`callback` is invoked.

If :var:`after` is 0., the :type:`flux_watcher_t` will be immediately ready
when the reactor is started. If :var:`repeat` is 0., the :type:`flux_watcher_t`
will automatically be stopped when :var:`after` seconds have elapsed.

Note that :var:`after` is internally referenced to reactor time, which is
only updated when the reactor is run/created, and therefore
can be out of date. Use :man3:`flux_reactor_now_update` to manually
update reactor time before creating timer watchers in such cases.
Refer to "The special problem of time updates" in the libev manual
for more information.

To restart a timer that has been automatically stopped, you must reset
the :var:`after` and :var:`repeat` values with :func:`flux_timer_watcher_reset`
before calling :man3:`flux_watcher_start`.

The callback :var:`revents` argument should be ignored.

Note: the Flux reactor is based on libev. For additional information
on the behavior of timers, refer to the libev documentation on ``ev_timer``.


RETURN VALUE
============

:func:`flux_timer_watcher_create` returns a :type:`flux_watcher_t` object
on success.  On error, NULL is returned, and :var:`errno` is set appropriately.


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

:man3:`flux_watcher_start`, :man3:`flux_reactor_run`, :man3:`flux_reactor_now`
