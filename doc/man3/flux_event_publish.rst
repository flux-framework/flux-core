=====================
flux_event_publish(3)
=====================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   flux_future_t *flux_event_publish (flux_t *h,
                                      const char *topic,
                                      int flags,
                                      const char *s);

   flux_future_t *flux_event_publish_pack (flux_t *h,
                                           const char *topic,
                                           int flags,
                                           const char *fmt,
                                           ...);

   flux_future_t *flux_event_publish_raw (flux_t *h,
                                          const char *topic,
                                          int flags,
                                          const void *data,
                                          size_t len);

   int flux_event_publish_get_seq (flux_future_t *f, int *seq);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_event_publish` sends an event message with topic string
:var:`topic`, :var:`flags` as described below, and optional payload :var:`s`,
a NULL-terminated string, or NULL indicating no payload. The returned future
is fulfilled once the event is accepted by the broker and assigned a
global sequence number.

:func:`flux_event_publish_pack` is similar, except the JSON payload
is constructed using :func:`json_pack` style arguments (see below).

:func:`flux_event_publish_raw` is similar, except the payload is raw
:var:`data` of length :var:`len`.

:func:`flux_event_publish_get_seq` may be used to retrieve the sequence
number assigned to the message once the future is fulfilled.


CONFIRMATION SEMANTICS
======================

All Flux events are "open loop" in the sense that publishers get no
confirmation that subscribers have received a message. However,
the above functions do confirm, upon fulfillment of the returned future,
that the published event has been received by the broker and assigned
a global sequence number.

Gaps in the sequence trigger the logging of errors currently, and in
the future will trigger recovery of lost events, so these confirmations
do indicate that Flux's best effort at event propagation is under way.

If this level of confirmation is not required, one may encode
an event message directly using :man3:`flux_event_encode` and related
functions and send it directly with :man3:`flux_send`.


FLAGS
=====

The :var:`flags` argument in the above functions must be zero, or the
logical OR of the following values:

FLUX_MSGFLAG_PRIVATE
   Indicates that the event should only be visible to the instance owner
   and the sender.

ENCODING JSON PAYLOADS
======================

.. include:: common/json_pack.rst


RETURN VALUE
============

These functions return a future on success. On error, NULL is returned,
and errno is set appropriately.


ERRORS
======

EINVAL
   Some arguments were invalid.

ENOMEM
   Out of memory.

EPROTO
   A protocol error was encountered.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_event_decode`, :man3:`flux_event_subscribe`
