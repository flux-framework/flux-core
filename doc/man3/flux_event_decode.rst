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
                              int *len);

   int flux_event_unpack (const flux_msg_t *msg,
                          const char **topic,
                          const char *fmt,
                          ...);

   flux_msg_t *flux_event_encode (const char *topic, const char *s);

   flux_msg_t *flux_event_encode_raw (const char *topic,
                                      const void *data,
                                      int len);

   flux_msg_t *flux_event_pack (const char *topic,
                                const char *fmt,
                                ...);


DESCRIPTION
===========

``flux_event_decode()`` decodes a Flux event message *msg*.

*topic*, if non-NULL, will be set to the message's topic string. The storage
for this string belongs to *msg* and should not be freed.

*s*, if non-NULL, will be set to the message's NULL-terminated string payload.
If no payload exists, it is set to NULL. The storage for this string belongs
to *msg* and should not be freed.

``flux_event_decode_raw()`` decodes an event message with a raw payload,
setting *data* and *len* to the payload data and length. The storage for
the raw payload belongs to *msg* and should not be freed.

``flux_event_unpack()`` decodes a Flux event message with a JSON payload as
above, parsing the payload using variable arguments with a format string
in the style of jansson's ``json_unpack()`` (used internally). Decoding fails
if the message doesn't have a JSON payload.

``flux_event_encode()`` encodes a Flux event message with topic string *topic*
and optional NULL-terminated string payload *s*. The newly constructed
message that is returned must be destroyed with ``flux_msg_destroy()``.

``flux_event_encode_raw()`` encodes a Flux event message with topic
string *topic*. If *data* is non-NULL, its contents will be used as
the message payload, and the payload type set to raw.

``flux_event_pack()`` encodes a Flux event message with a JSON payload as
above, encoding the payload using variable arguments with a format string
in the style of jansson's ``json_pack()`` (used internally). Decoding fails
if the message doesn't have a JSON payload.

Events propagated to all subscribers. Events will not be received
without a matching subscription established using ``flux_event_subscribe()``.

ENCODING JSON PAYLOADS
======================

.. include:: common/json_pack.rst

DECODING JSON PAYLOADS
======================

.. include:: common/json_unpack.rst


RETURN VALUE
============

Decoding functions return 0 on success. On error, -1 is returned, and
errno is set appropriately.

Encoding functions return a message on success. On error, NULL is returned,
and errno is set appropriately.


ERRORS
======

EINVAL
   The *msg* argument was NULL or there was a problem encoding.

ENOMEM
   Memory was unavailable.

EPROTO
   Message decoding failed, such as due to incorrect message type,
   missing topic string, etc.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_event_subscribe`
