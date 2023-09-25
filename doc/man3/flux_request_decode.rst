======================
flux_request_decode(3)
======================


SYNOPSIS
========

::

   #include <flux/core.h>

::

   int flux_request_decode (const flux_msg_t *msg,
                            const char **topic,
                            const char **s);

::

   int flux_request_unpack (const flux_msg_t *msg,
                            const char **topic,
                            const char *fmt, ...);

::

   int flux_request_decode_raw (const flux_msg_t *msg,
                                const char **topic,
                                const void **data, int *len);


DESCRIPTION
===========

``flux_request_decode()`` decodes a request message *msg*.

*topic*, if non-NULL, will be set the message's topic string. The storage
for this string belongs to *msg* and should not be freed.

*s*, if non-NULL, will be set to the message's NULL-terminated string payload.
If no payload exists, it is set to NULL. The storage for this string belongs
to *msg* and should not be freed.

``flux_request_unpack()`` decodes a request message with a JSON payload as
above, parsing the payload using variable arguments with a format string
in the style of jansson's ``json_unpack()`` (used internally). Decoding fails
if the message doesn't have a JSON payload.

``flux_request_decode_raw()`` decodes a request message with a raw payload,
setting *data* and *len* to the payload data and length. The storage for
the raw payload belongs to *msg* and should not be freed.

DECODING JSON PAYLOADS
======================

.. include:: common/json_unpack.rst


RETURN VALUE
============

These functions return 0 on success. On error, -1 is returned, and
errno is set appropriately.


ERRORS
======

EINVAL
   The *msg* argument was NULL.

EPROTO
   Message decoding failed, such as due to incorrect message type,
   missing topic string, etc.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_respond`, :man3:`flux_rpc`
