============
flux_recv(3)
============


SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  flux_msg_t *flux_recv (flux_t *h,
                         struct flux_match match,
                         int flags);


DESCRIPTION
===========

``flux_recv()`` receives a message using the Flux Message broker,
previously opened with ``flux_open()`` on handle *h*.
The message should eventually be destroyed with ``flux_msg_destroy()``.

*match* is a message match structure which limits which messages
can be received.

::

   struct flux_match {
       int typemask;      // bitmask of matching message types
       uint32_t matchtag; // matchtag
       char *topic_glob;  // glob matching topic string
   };

The following initializers are available for *match*:

FLUX_MATCH_ANY
   Match any message.

FLUX_MATCH_EVENT
   Match any event message.

For additional details on how to use *match*, see :man3:`flux_msg_cmp`.

*flags* is the logical "or" of zero or more of the following flags:

FLUX_O_TRACE
   Dumps *msg* to stderr.

FLUX_O_NONBLOCK
   If unable to receive a matching message, return an error rather than block.

Internally, flags are the logical "or" of *flags* and the flags provided
to ``flux_open()`` when the handle was created.

Messages that do not meet *match* criteria, are requeued with
``flux_requeue()`` for later consumption.


RETURN VALUE
============

``flux_recv()`` returns a message on success. On error, NULL is returned,
and errno is set appropriately.


ERRORS
======

ENOSYS
   Handle has no recv operation.

EINVAL
   Some arguments were invalid.

EAGAIN
   ``FLUX_O_NONBLOCK`` was selected and ``flux_send()`` would block.


EXAMPLES
========

This example opens the Flux broker and displays event messages
as they arrive.

.. literalinclude:: trecv.c


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_open`, :man3:`flux_send`, :man3:`flux_requeue`, :man3:`flux_msg_cmp`
