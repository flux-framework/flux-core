===========================
flux_stat_watcher_create(3)
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

   flux_watcher_t *flux_stat_watcher_create (flux_reactor_t *r,
                                             const char *path,
                                             double interval,
                                             flux_watcher_f callback,
                                             void *arg);

::

   void flux_stat_watcher_get_rstat (flux_watcher_t *w,
                                     struct stat *stat,
                                     struct stat *prev);


DESCRIPTION
===========

``flux_stat_watcher_create()`` creates a reactor watcher that
monitors for changes in the status of the file system object
represented by *path*. If the file system object exists,
inotify(2) is used, if available; otherwise the reactor polls
the file every *interval* seconds. A value of zero selects a
conservative default (currently five seconds).

The callback *revents* argument should be ignored.

``flux_stat_watcher_get_rstat ()`` may be used to obtain the status
within *callback*. If non-NULL, *stat* receives the current status.
If non-NULL, *prev* receives the previous status.

If the object does not exist, stat->st_nlink will be zero and other
status fields are undefined. The appearance/disappearance of a file
is considered a status change like any other.


RETURN VALUE
============

flux_stat_watcher_create() returns a flux_watcher_t object on success.
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

flux_watcher_start(3), flux_reactor_start(3), stat(2)

`libev home page <http://software.schmorp.de/pkg/libev.html>`__
