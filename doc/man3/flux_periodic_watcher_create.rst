===============================
flux_periodic_watcher_create(3)
===============================


SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   typedef void (*flux_watcher_f)(flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents,
                                  void *arg);

   typedef double (*flux_reschedule_f)(flux_watcher_t *w,
                                       double now,
                                       void *arg);

   flux_watcher_t *flux_periodic_watcher_create (
                                         flux_reactor_t *r,
                                         double offset,
                                         double interval,
                                         flux_reschedule_f resched_cb,
                                         flux_watcher_f callback,
                                         void *arg);

   void flux_periodic_watcher_reset (flux_watcher_t *w,
                                     double offset,
                                     double interval);


DESCRIPTION
===========

``flux_periodic_watcher_create()`` creates a flux_watcher_t object which
monitors for periodic events. The periodic watcher will trigger when the
wall clock time *offset* has elapsed, and optionally again ever *interval*
of wall clock time thereafter. If the *reschedule_cb* parameter is used,
then *offset* and *interval* are ignored, and instead each time the
periodic watcher is scheduled the reschedule callback will be called
with the current time, and is expected to return the next absolute time
at which the watcher should be scheduled.

Unlike timer events, a periodic watcher monitors wall clock or system time,
not the actual time that passes. Thus, a periodic watcher can be used
to run a callback when system time reaches a certain point. For example,
if a periodic watcher is set to run with an *offset* of 10 seconds, and
then system time is set back by 1 hour, it will take approximately 1 hour,
10 seconds for the watcher to execute.

If a periodic watcher is running in manual reschedule mode (*reschedule_cb*
is non-NULL), and the user-provided reschedule callback returns a time
that is before the current time, the watcher will be silently stopped.

Note: the Flux reactor is based on libev. For additional important
information on the behavior of periodic, refer to the libev documentation
on ``ev_periodic``.


RETURN VALUE
============

``flux_periodic_watcher_create()`` returns a flux_watcher_t object on success.
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

:man3:`flux_watcher_start`, :man3:`flux_reactor_run`, :man3:`flux_timer_watcher_create`
