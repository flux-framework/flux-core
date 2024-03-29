============
flux_send(3)
============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   int flux_send (flux_t *h, const flux_msg_t *msg, int flags);

   int flux_send_new (flux_t *h, flux_msg_t **msg, int flags);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_send` sends :var:`msg` using the Flux Message broker,
previously opened with :man3:`flux_open` on handle :var:`h`.

:var:`flags` is the logical "or" of zero or more of the following flags:

FLUX_O_TRACE
   Dumps :var:`msg` to stderr.

FLUX_O_NONBLOCK
   If unable to send, return an error rather than block.

Internally, flags are the logical "or" of :var:`flags` and the flags provided
to :man3:`flux_open` when the handle was created.

The message type, topic string, and nodeid affect how the message
will be routed by the broker. These attributes are pre-set in the message.

:func:`flux_send_new` is the same, except message ownership is transferred
to the handle :var:`h`.  The double pointer :var:`msg` points to a NULL value if
the message is successfully transferred.  The send fails if the message
reference count is greater than one.


RETURN VALUE
============

:func:`flux_send` returns zero on success. On error, -1 is returned, and
:var:`errno` is set appropriately.


ERRORS
======

ENOSYS
   Handle has no send operation.

EINVAL
   Some arguments were invalid.

EAGAIN
   ``FLUX_O_NONBLOCK`` was selected and :func:`flux_send` would block.


EXAMPLES
========

This example opens the Flux broker and publishes an event message.

.. literalinclude:: example/send.c
  :language: c


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_open`, :man3:`flux_recv`, :man3:`flux_requeue`
