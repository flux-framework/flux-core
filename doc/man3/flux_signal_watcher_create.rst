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

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_signal_watcher_create` creates a reactor watcher that
monitors for receipt of signal :var:`signum`.

The callback :var:`revents` argument should be ignored.

When one :var:`callback` is shared by multiple watchers, the signal number that
triggered the event can be obtained with
:func:`flux_signal_watcher_get_signum`.

Signal handling can be tricky in multi-threaded programs.  It is advisable
to handle signals in the main thread only.  For example, block signals by
calling :linux:man2:`sigprocmask` before spawning other threads, and register
signal watchers only in the main thread.

RETURN VALUE
============

:func:`flux_signal_watcher_create` returns a :type:`flux_watcher_t` object
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

:man3:`flux_watcher_start`, :man3:`flux_reactor_run`
