=============================
flux_signal_watcher_create(3)
=============================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   typedef void (*flux_watcher_f)(flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents,
                                  void *arg);

   flux_watcher_t *flux_signal_watcher_create (flux_reactor_t *r,
                                               int signum,
                                               flux_watcher_f callback,
                                               void *arg);

   int flux_signal_watcher_get_signum (flux_watcher_t *w);


DESCRIPTION
===========

:func:`flux_signal_watcher_create` creates a reactor watcher that
monitors for receipt of signal *signum*.

The callback *revents* argument should be ignored.

When one *callback* is shared by multiple watchers, the signal number that
triggered the event can be obtained with
:func:`flux_signal_watcher_get_signum`.


RETURN VALUE
============

:func:`flux_signal_watcher_create` returns a flux_watcher_t object on success.
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

:man3:`flux_watcher_start`, :man3:`flux_reactor_run`
