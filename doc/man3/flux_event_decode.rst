====================
flux_event_decode(3)
====================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   int flux_event_decode (const flux_msg_t *msg,
                          const char **topic,
                          const char **s);

   int flux_event_decode_raw (const flux_msg_t *msg,
                              const char **topic,
                              const void **data,
                              size_t *len);

   int flux_event_unpack (const flux_msg_t *msg,
                          const char **topic,
                          const char *fmt,
                          ...);

   flux_msg_t *flux_event_encode (const char *topic, const char *s);

   flux_msg_t *flux_event_encode_raw (const char *topic,
                                      const void *data,
                                      size_t len);

   flux_msg_t *flux_event_pack (const char *topic,
                                const char *fmt,
                                ...);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_event_decode` decodes a Flux event message :var:`msg`.

:var:`topic`, if non-NULL, will be set to the message's topic string. The
storage for this string belongs to :var:`msg` and should not be freed.

:var:`s`, if non-NULL, will be set to the message's NULL-terminated string
payload.  If no payload exists, it is set to NULL. The storage for this string
belongs to :var:`msg` and should not be freed.

:func:`flux_event_decode_raw` decodes an event message with a raw payload,
setting :var:`data` and :var:`len` to the payload data and length. The storage
for the raw payload belongs to :var:`msg` and should not be freed.

:func:`flux_event_unpack` decodes a Flux event message with a JSON payload as
above, parsing the payload using variable arguments with a format string
in the style of jansson's :func:`json_unpack` (used internally). Decoding fails
if the message doesn't have a JSON payload.

:func:`flux_event_encode` encodes a Flux event message with topic string
:var:`topic` and optional NULL-terminated string payload :var:`s`. The newly
constructed message that is returned must be destroyed with
:func:`flux_msg_destroy()`.

:func:`flux_event_encode_raw` encodes a Flux event message with topic
string :var:`topic`. If :var:`data` is non-NULL, its contents will be used as
the message payload, and the payload type set to raw.

:func:`flux_event_pack` encodes a Flux event message with a JSON payload as
above, encoding the payload using variable arguments with a format string
in the style of jansson's :func:`json_pack` (used internally). Decoding fails
if the message doesn't have a JSON payload.

Events propagated to all subscribers. Events will not be received
without a matching subscription established using :func:`flux_event_subscribe`.

ENCODING JSON PAYLOADS
======================

.. include:: common/json_pack.rst

DECODING JSON PAYLOADS
======================

.. include:: common/json_unpack.rst


RETURN VALUE
============

Decoding functions return 0 on success. On error, -1 is returned, and
:var:`errno` is set appropriately.

Encoding functions return a message on success. On error, NULL is returned,
and :var:`errno` is set appropriately.


ERRORS
======

EINVAL
   The :var:`msg` argument was NULL or there was a problem encoding.

ENOMEM
   Memory was unavailable.

EPROTO
   Message decoding failed, such as due to incorrect message type,
   missing topic string, etc.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_event_subscribe`
