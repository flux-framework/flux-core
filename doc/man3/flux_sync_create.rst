===================
flux_sync_create(3)
===================


SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   flux_future_t *flux_sync_create (flux_t *h, double minimum);


DESCRIPTION
===========

``flux_sync_create()`` creates a future that is fulfilled when
the system heartbeat message is received.  System heartbeats are
event messages published periodically at a configurable interval.
Synchronizing Flux internal overhead to the heartbeat can, in theory,
reduce disruption to bulk synchronous applications.

If *minimum* is greater than zero, it establishes a minimum time in seconds
between fulfillments.  Heartbeats that arrive too soon after the last one
are ignored.  This may be used to protect from thrashing if the heartbeat
period is set too fast, or if heartbeats arrive close to one another in time
due to overlay network congestion.

A maximum time between fulfillments may be established by specifying a
continuation timeout with ``flux_future_then()``.  If the timeout expires,
the future is fulfilled with an error (ETIMEDOUT), as usual.

On each fulfillment, ``flux_future_reset()`` should be called to enable
the future to be fulfilled again, and to re-start any timeout.


RETURN VALUE
============

``flux_sync_create()`` returns a future, or NULL on failure with
errno set.


ERRORS
======

EINVAL
   One or more arguments were invalid.

ENOMEM
   Out of memory.


EXAMPLE
=======

Set up a continuation callback for each heartbeat that arrives at least
*sync_min* seconds from the last, with a timeout of *sync_max* seconds:


.. literalinclude:: example/sync.c
  :language: c


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_future_then`, :man3:`flux_future_get`, :man3:`flux_future_reset`
