=========================
flux_fd_watcher_create(3)
=========================


SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   typedef void (*flux_watcher_f)(flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents,
                                  void *arg);

   flux_watcher_t *flux_fd_watcher_create (flux_reactor_t *r,
                                           int fd,
                                           int events,
                                           flux_watcher_f callback,
                                           void *arg);

   int flux_fd_watcher_get_fd (flux_watcher_t *w);


DESCRIPTION
===========

``flux_fd_watcher_create()`` creates a flux_watcher_t object which can be used
to monitor for events on a file descriptor *fd*. When events occur,
the user-supplied *callback* is invoked.

The *events* and *revents* arguments are a bitmask containing a logical
OR of the following bits. If a bit is set in *events*, it indicates
interest in this type of event. If a bit is set in *revents*, it
indicates that this event has occurred.

FLUX_POLLIN
   The file descriptor is ready for reading.

FLUX_POLLOUT
   The file descriptor is ready for writing.

FLUX_POLLERR
   The file descriptor has encountered an error.
   This bit is ignored if it is set in the create *events* argument.

Events are processed in a level-triggered manner. That is, the callback
will continue to be invoked as long as the event has not been
fully consumed or cleared, and the watcher has not been stopped.

``flux_fd_watcher_get_fd()`` is used to obtain the file descriptor from
within the flux_watcher_f callback.


RETURN VALUE
============

``flux_fd_watcher_create()`` returns a flux_watcher_t object on success.
On error, NULL is returned, and errno is set appropriately.

``flux_fd_watcher_get_fd()`` returns the file descriptor associated with
the watcher.


ERRORS
======

ENOMEM
   Out of memory.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_watcher_start`, :man3:`flux_reactor_run`
