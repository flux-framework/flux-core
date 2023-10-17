===============
flux_requeue(3)
===============


SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  cc $(pkg-config --cflags --libs flux-core)


DESCRIPTION
===========

.. function:: int flux_requeue(flux_t *h, const flux_msg_t *msg)

Enqueue :c:var:`msg` on the head of the handle :c:var:`h`'s receive queue,
placing it in front of any messages already in the queue.  If successful, a
reference is taken on :c:var:`msg` and the handle :c:var:`h` becomes
ready for reading just as if the message were received from the broker.
Available messages may be read by calling :man3:`flux_recv`.

.. function:: int flux_enqueue(flux_t *h, const flux_msg_t *msg)

Like :func:`flux_requeue` except the message is enqueued on the tail of the
receive queue, placing it behind any messages already in the queue.


RETURN VALUE
============

These functions return zero on success.  On error, -1 is returned with errno
set.


ERRORS
======

EINVAL
   Some arguments were invalid.

ENOMEM
   Out of memory.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_open`, :man3:`flux_recv`, :man3:`flux_send`
