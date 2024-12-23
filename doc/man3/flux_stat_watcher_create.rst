===========================
flux_stat_watcher_create(3)
===========================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   typedef void (*flux_watcher_f)(flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents,
                                  void *arg);

   flux_watcher_t *flux_stat_watcher_create (flux_reactor_t *r,
                                             const char *path,
                                             double interval,
                                             flux_watcher_f callback,
                                             void *arg);

   int flux_stat_watcher_get_rstat (flux_watcher_t *w,
                                    struct stat *stat,
                                    struct stat *prev);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_stat_watcher_create` creates a reactor watcher that
monitors for changes in the status of the file system object
represented by :var:`path`. If the file system object exists,
:linux:man7:`inotify` is used, if available; otherwise the reactor polls
the file every :var:`interval` seconds. A value of zero selects a
conservative default (currently five seconds).

The callback :var:`revents` argument should be ignored.

:func:`flux_stat_watcher_get_rstat` may be used to obtain the status
within :var:`callback`. If non-NULL, :var:`stat` receives the current status.
If non-NULL, :var:`prev` receives the previous status.

If the object does not exist, :var:`stat->st_nlink` will be zero and other
status fields are undefined. The appearance/disappearance of a file
is considered a status change like any other.


RETURN VALUE
============

:func:`flux_stat_watcher_create` returns a :type:`flux_watcher_t` object
on success.  On error, NULL is returned, and :var:`errno` is set appropriately.

:func:`flux_stat_watcher_get_rstat` returns 0 on success.  On error, -1 is
returned and :var:`errno` is set appropriately.


ERRORS
======

ENOMEM
   Out of memory.

EINVAL
   Invalid argument.


RESOURCES
=========

.. include:: common/resources.rst

libev: http://software.schmorp.de/pkg/libev.html


SEE ALSO
========

:man3:`flux_watcher_start`, :man3:`flux_reactor_run`,
:linux:man2:`stat`
