==================
flux_pollevents(3)
==================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  int flux_pollevents (flux_t *h);

  int flux_pollfd (flux_t *h);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_pollevents` returns a bitmask of poll flags for handle :var:`h`.

:func:`flux_pollfd` obtains a file descriptor that becomes readable when
:func:`flux_pollevents` has poll flags raised.  On Linux, it is an
:linux:man7:`epoll` edge-triggered file descriptor.  The file descriptor
is the property of :var:`h` and becomes invalid upon :man3:`flux_close`.

Valid poll flags are:

FLUX_POLLIN
   Handle is ready for reading.

FLUX_POLLOUT
   Handle is ready for writing.

FLUX_POLLERR
   Handle has experienced an error.

These functions can be used to integrate a :type:`flux_t` handle into any
event loop.  The event loop would watch the :func:`flux_pollfd` for POLLIN
events, then when one is raised, check :func:`flux_pollevents` for pending
events.

Due to edge triggering, the event loop must process all events on the handle
before returning to sleep.

When processing an edge-triggered event source, it is a best practice to
maintain fairness by allowing other event sources to receive attention while
working through pending I/O on the edge-triggered source.  This can be
accomplished in the Flux reactor by combining prep, check, and idle watchers
into a composite watcher for the edge triggered source.  This is how
:man3:`flux_handle_watcher_create` is implemented internally.

RETURN VALUE
============

:func:`flux_pollevents` returns flags on success. On error, -1 is returned,
and :var:`errno` is set.

:func:`flux_pollfd` returns a file descriptor on success. On error, -1 is
returned, and :var:`errno` is set.


ERRORS
======

EINVAL
   Some arguments were invalid.


RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man3:`flux_open`

