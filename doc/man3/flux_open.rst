============
flux_open(3)
============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   flux_t *flux_open (const char *uri, int flags);

   flux_t *flux_open_ex (const char *uri,
                         int flags,
                         flux_error_t *error);

   void flux_close (flux_t *h);

   flux_t *flux_clone (flux_t *h);

   int flux_reconnect (flux_t *h);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_open` and :func:`flux_open_ex` create a :type:`flux_t` handle, used
to communicate with the Flux message broker. :func:`flux_open_ex` takes an
optional pointer to a :type:`flux_error_t` structure which, when non-NULL, will
be used to store any errors which may have otherwise gone to :var:`stderr`.

When set, the :var:`uri` argument may be a valid native URI, e.g. as
returned from the :man1:`flux-uri` command or found in the :var:`local-uri`
and :var:`parent-uri` broker attributes, or a path-like string referencing
a possible instance ancestor like "/" for the top-level or root instance,
or ".." for the parent instance. See `ANCESTOR PATHS`_ below.

If :var:`uri` is NULL, the value of :envvar:`FLUX_URI` is used.
If :envvar:`FLUX_URI` is not set, a compiled-in default URI is used.

When :var:`uri` contains a native URI, the scheme (before "://") specifies
the "connector" that will be used to establish the connection. The :var:`uri`
path (after "://") is parsed by the connector.

*flags* is the logical "or" of zero or more of the following flags:

FLUX_O_TRACE
   Dumps message trace to stderr.

FLUX_O_CLONE
   Used internally by :func:`flux_clone` (see below).

FLUX_O_MATCHDEBUG
   Prints diagnostic to stderr when matchtags are leaked, for example when
   a streaming RPC is destroyed without receiving a error response as
   end-of-stream, or a regular RPC is destroyed without being fulfilled at all.

FLUX_O_NONBLOCK
   The :man3:`flux_send` and :man3:`flux_recv` functions should never block.

FLUX_O_TEST_NOSUB
   Make :man3:`flux_event_subscribe` and :man3:`flux_event_unsubscribe` no-ops.
   This may be useful in specialized situations with the ``loop://`` connector,
   where no message handler is available to service subscription RPCs.

FLUX_O_RPCTRACK
   Track pending RPCs so that they can receive automatic ECONNRESET failure
   responses if the broker connection is re-established with
   :func:`flux_reconnect`.  Tracking incurs a small overhead.  This flag can
   only be specified with :func:`flux_open`, not :man3:`flux_flags_set`.

:func:`flux_reconnect` may be called from a communications error callback
registered with :man3:`flux_comms_error_set`.  The current connection is
closed and a new one is established with a new UUID.  Since responses addressed
to the old UUID will not be routed to the new connection, RPCs that are pending
before :func:`flux_reconnect` remain pending indefinitely without
FLUX_O_RPCTRACK.  After a successful reconnect, the following additional steps
may be needed before a client can continue normal operation:

- Wait until the broker has entered RUN state by making an RPC to ``state_machine.wait``
- Restore service registrations.
- Restore event subscriptions.

:func:`flux_clone` creates another reference to a :type:`flux_t` handle that is
identical to the original in all respects except that it does not inherit
a copy of the original handle's "aux" hash, or its reactor and message
dispatcher references. By creating a clone, and calling
:man3:`flux_set_reactor` on it, one can create message handlers on the cloned
handle that run on a different reactor than the one associated with the
original handle.

:func:`flux_close` destroys a :type:`flux_t` handle, closing its connection with
the Flux message broker.


ANCESTOR PATHS
==============

As an alternative to a URI, the :func:`flux_open` and :func:`flux_open_ex`
functions also take a path-like string indicating that a handle to an ancestor
instance should be opened. This string follows the following rules:

 - One or more ".." separated by "/" refer to a parent instance
   (possibly multiple levels up the hierarchy)
 - "." indicates the current instance
 - A single slash ("/") indicates the root instance
 - ".." at the root is not an error, it just returns a handle to the root
   instance

RETURN VALUE
============

:func:`flux_open` and :func:`flux_clone` return a :type:`flux_t` handle on
success.  On error, NULL is returned, with :var:`errno` set.


ERRORS
======

EINVAL
   One or more arguments was invalid.

ENOMEM
   Out of memory.


EXAMPLES
========

This example opens the Flux broker using the default connector
and path, requests the broker rank, and finally closes the broker handle.

.. literalinclude:: example/open.c
  :language: c


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-uri`, :man3:`flux_comms_error_set`
