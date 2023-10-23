=============================
flux_handle_watcher_create(3)
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

   flux_watcher_t *flux_handle_watcher_create (flux_reactor_t *r,
                                               flux_t *h,
                                               int events,
                                               flux_watcher_f callback,
                                               void *arg);

   flux_t *flux_handle_watcher_get_flux (flux_watcher_t *w);


DESCRIPTION
===========

:func:`flux_handle_watcher_create` creates a :type:`flux_watcher_t` object
which monitors for events on a Flux broker handle :var:`h`. When events occur,
the user-supplied :var:`callback` is invoked.

The :var:`events` and :var:`revents` arguments are a bitmask containing a
logical OR of the following bits. If a bit is set in :var:`events`,
it indicates interest in this type of event. If a bit is set in :var:`revents`,
it indicates that this event has occurred.

FLUX_POLLIN
   The handle is ready for reading.

FLUX_POLLOUT
   The handle is ready for writing.

FLUX_POLLERR
   The handle has encountered an error.
   This bit is ignored if it is set in :var:`events`.

Events are processed in a level-triggered manner. That is, the
callback will continue to be invoked as long as the event has not been
fully consumed or cleared, and the watcher has not been stopped.

:func:`flux_handle_watcher_get_flux` is used to obtain the handle from
within the callback.


RETURN VALUE
============

:func:`flux_handle_watcher_create` returns a :type:`flux_watcher_t` object
on success.  On error, NULL is returned, and errno is set appropriately.

:func:`flux_handle_watcher_get_flux` returns the handle associated with
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

:man3:`flux_watcher_start`, :man3:`flux_reactor_run`,
:man3:`flux_recv`, :man3:`flux_send`
