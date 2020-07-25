===============
flux_requeue(3)
===============


SYNOPSIS
========

#include <flux/core.h>

int flux_requeue (flux_t \*h, const flux_msg_t \*msg, int flags);

int flux_requeue_nocopy (flux_t \*h, flux_msg_t \*msg, int flags);


DESCRIPTION
===========

``flux_requeue()`` requeues a *msg* in handle *h*. The message
can be received with ``flux_recv()`` as though it arrived from the broker.

``flux_requeue_nocopy()`` is identical to ``flux_requeue()``, but does not
copy the message. It instead takes ownership of the *msg* passed in
by the caller.

*flags* must be set to one of the following values:

FLUX_RQ_TAIL
   *msg* is placed at the tail of the message queue.

FLUX_RQ_TAIL
   *msg* is placed at the head of the message queue.


RETURN VALUE
============

``flux_requeue()`` and ``flux_requeue_nocopy()`` return zero on success.
On error, -1 is returned, and errno is set appropriately.


ERRORS
======

EINVAL
   Some arguments were invalid.

ENOMEM
   Out of memory.


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

flux_open(3), flux_recv(3), flux_send(3)
