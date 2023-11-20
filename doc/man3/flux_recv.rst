============
flux_recv(3)
============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  flux_msg_t *flux_recv (flux_t *h,
                         struct flux_match match,
                         int flags);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_recv` receives a message using the Flux Message broker,
previously opened with :man3:`flux_open` on handle :var:`h`.
The message should eventually be destroyed with :man3:`flux_msg_destroy`.

:var:`match` is a message match structure which limits which messages
can be received.

::

   struct flux_match {
       int typemask;      // bitmask of matching message types
       uint32_t matchtag; // matchtag
       char *topic_glob;  // glob matching topic string
   };

The following initializers are available for :var:`match`:

FLUX_MATCH_ANY
   Match any message.

FLUX_MATCH_EVENT
   Match any event message.

For additional details on how to use :var:`match`, see :man3:`flux_msg_cmp`.

:var:`flags` is the logical "or" of zero or more of the following flags:

FLUX_O_TRACE
   Dumps :var:`msg` to stderr.

FLUX_O_NONBLOCK
   If unable to receive a matching message, return an error rather than block.

Internally, flags are the logical "or" of :var:`flags` and the flags provided
to :man3:`flux_open` when the handle was created.

Messages that do not meet :var:`match` criteria, are requeued with
:man3:`flux_requeue` for later consumption.


RETURN VALUE
============

:func:`flux_recv` returns a message on success. On error, NULL is returned,
and :var:`errno` is set appropriately.


ERRORS
======

ENOSYS
   Handle has no recv operation.

EINVAL
   Some arguments were invalid.

EAGAIN
   ``FLUX_O_NONBLOCK`` was selected and :func:`flux_recv` would block.


EXAMPLES
========

This example opens the Flux broker and displays event messages
as they arrive.

.. literalinclude:: example/recv.c
  :language: c


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_open`, :man3:`flux_send`, :man3:`flux_requeue`, :man3:`flux_msg_cmp`
