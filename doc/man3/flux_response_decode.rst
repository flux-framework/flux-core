=======================
flux_response_decode(3)
=======================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   int flux_response_decode (const flux_msg_t *msg,
                             const char **topic,
                             const char **s);

   int flux_response_decode_raw (const flux_msg_t *msg,
                                 const char **topic,
                                 const void **data,
                                 size_t *len);

   int flux_response_decode_error (const flux_msg_t *msg,
                                   const char *errstr);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_response_decode` decodes a response message :var:`msg`.

:var:`topic`, if non-NULL, will be set to the message's topic string. The
storage for this string belongs to :var:`msg` and should not be freed.

:var:`s`, if non-NULL, will be set to the message's NULL-terminated string
payload.  If no payload exists, it is set to NULL. The storage for this
string belongs to :var:`msg` and should not be freed.

:func:`flux_response_decode_raw` decodes a response message with a raw payload,
setting :var:`data` and :var:`len` to the payload data and length. The storage
for the raw payload belongs to :var:`msg` and should not be freed.

:func:`flux_response_decode_error` decodes an optional error string included
with an error response. This fails if the response is not an error,
or does not include an error string payload.


RETURN VALUE
============

These functions return 0 on success. On error, -1 is returned, and
:var:`errno` is set appropriately.


ERRORS
======

EINVAL
   The :var:`msg` argument was NULL.

EPROTO
   Message decoding failed, such as due to incorrect message type,
   missing topic string, etc.

ENOENT
   :func:`flux_response_decode_error` was called on a message with no
   error response payload.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_request_encode`, :man3:`flux_rpc`
