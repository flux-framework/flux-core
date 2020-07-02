============
flux_send(3)
============


SYNOPSIS
========

#include <flux/core.h>

int flux_send (flux_t \*h, const flux_msg_t \*msg, int flags);


DESCRIPTION
===========

``flux_send()`` sends *msg* using the Flux Message broker,
previously opened with ``flux_open()`` on handle *h*.

*flags* is the logical "or" of zero or more of the following flags:

FLUX_O_TRACE
   Dumps *msg* to stderr.

FLUX_O_NONBLOCK
   If unable to send, return an error rather than block.

Internally, flags are the logical "or" of *flags* and the flags provided
to ``flux_open()`` when the handle was created.

The message type, topic string, and nodeid affect how the message
will be routed by the broker. These attributes are pre-set in the message.


RETURN VALUE
============

``flux_send()`` returns zero on success. On error, -1 is returned, and errno
is set appropriately.


ERRORS
======

ENOSYS
   Handle has no send operation.

EINVAL
   Some arguments were invalid.

EAGAIN
   ``FLUX_O_NONBLOCK`` was selected and ``flux_send()`` would block.


EXAMPLES
========

This example opens the Flux broker and publishes an event message.

.. literalinclude:: tsend.c


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

flux_open(3), flux_recv(3), flux_requeue(3)
