==================
flux_pollevents(3)
==================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  int flux_pollfd (flux_t *h);

  int flux_pollevents (flux_t *h);


Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_pollfd` and :func:`flux_pollevents` are used together to
integrate a :type:`flux_t` handle into an event loop.

:func:`flux_pollfd` obtains a file descriptor that becomes readable when
handle :var:`h` needs attention.  It is suitable for monitoring using
:linux:man2:`poll` or equivalent.  The signaling is edge-triggered, meaning
that POLLIN is raised when the handle becomes ready for reading or writing,
but is not re-raised if those conditions are still true when :func:`poll`
is re-entered.  The file descriptor is created on the first call to
:func:`flux_pollfd` or :func:`flux_pollevents`.  It is used for signaling
purposes only, not for I/O.

:func:`flux_pollevents` returns a bitmask of poll flags for handle :var:`h`
and clears the pending :func:`flux_pollfd` POLLIN event, if any.

Valid poll flags are:

FLUX_POLLIN
   Handle is ready for reading.

FLUX_POLLOUT
   Handle is ready for writing.

FLUX_POLLERR
   Handle has experienced an error.

As indicated above, the event loop must process all events on the handle
before returning to sleep, due to edge triggering.  When processing an
edge-triggered event source, it is a best practice to maintain fairness by
allowing other event sources to receive attention while working through
pending I/O on the edge-triggered source.  This can be accomplished in
the Flux reactor by combining prep, check, and idle watchers into a
composite watcher for the edge triggered source.  Other event loops such
as libev and libuv have similar concepts.  For a Flux example, refer to the
source code for :man3:`flux_handle_watcher_create`.

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

