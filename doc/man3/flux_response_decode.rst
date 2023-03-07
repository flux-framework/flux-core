=======================
flux_response_decode(3)
=======================


SYNOPSIS
========

::

   #include <flux/core.h>

::

   int flux_response_decode (const flux_msg_t *msg,
                             const char **topic,
                             const char **s);

::

   int flux_response_decode_raw (const flux_msg_t *msg,
                                 const char **topic,
                                 const void **data, int *len);

::

   int flux_response_decode_error (const flux_msg_t *msg,
                                   const char *errstr);


DESCRIPTION
===========

``flux_response_decode()`` decodes a response message *msg*.

*topic*, if non-NULL, will be set to the message's topic string. The
storage for this string belongs to *msg* and should not be freed.

*s*, if non-NULL, will be set to the message's NULL-terminated string payload.
If no payload exists, it is set to NULL. The storage for this
string belongs to *msg* and should not be freed.

``flux_response_decode_raw()`` decodes a response message with a raw payload,
setting *data* and *len* to the payload data and length. The storage for
the raw payload belongs to *msg* and should not be freed.

``flux_response_decode_error()`` decodes an optional error string included
with an error response. This fails if the response is not an error,
or does not include an error string payload.


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

ENOENT
   ``flux_response_decode_error()`` was called on a message with no
   error response payload.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_request_encode`, :man3:`flux_rpc`
