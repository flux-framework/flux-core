===============
flux_respond(3)
===============

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   int flux_respond (flux_t *h,
                     const flux_msg_t *request,
                     const char *s);

   int flux_respond_pack (flux_t *h,
                          const flux_msg_t *request,
                          const char *fmt,
                          ...);

   int flux_respond_raw (flux_t *h,
                         const flux_msg_t *request,
                         const void *data,
                         size_t length);

   int flux_respond_error (flux_t *h,
                           const flux_msg_t *request,
                           int errnum,
                           const char *errmsg);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_respond`, :func:`flux_respond_pack`, :func:`flux_respond_raw`, and
:func:`flux_respond_error` encode and send a response message on handle
:var:`h`, deriving topic string, matchtag, and route stack from the provided
:var:`request`.

:func:`flux_respond` sends a response to :var:`request`. If :var:`s` is
non-NULL, :func:`flux_respond` will send it as the response payload, otherwise
there will be no payload.

:func:`flux_respond_raw` is identical except if :var:`data` is non-NULL,
:func:`flux_respond_raw` will send it as the response payload.

:func:`flux_respond_pack` encodes a response message with a JSON payload,
building the payload using variable arguments with a format string in
the style of jansson's :func:`json_pack` (used internally).

:func:`flux_respond_error` returns an error response to the sender.
If :var:`errnum` is zero, EINVAL is used.  If :var:`errmsg` is non-NULL,
an error string payload is included in the response. The error string may be
used to provide a more detailed error message than can be conveyed via
:var:`errnum`.


STREAMING SERVICES
==================

Per RFC 6, a "streaming" service must return zero or more non-error
responses to a request and a final error response. If the requested
operation was successful, the final error response may use ENODATA as
the error number. Clients should interpret ENODATA as a non-error
end-of-stream marker.

It is essential that services which return multiple responses verify that
requests were made with the FLUX_RPC_STREAMING flag by testing the
FLUX_MSGFLAG_STREAMING flag, e.g. using :man3:`flux_msg_is_streaming`.
If the flag is not set, the service must return an immediate EPROTO error.

ENCODING JSON PAYLOADS
======================

.. include:: common/json_pack.rst


RETURN VALUE
============

These functions return zero on success. On error, -1 is returned,
and :var:`errno` is set appropriately.


ERRORS
======

ENOSYS
   Handle has no send operation.

EINVAL
   Some arguments were invalid.

EPROTO
   A protocol error was encountered.


RESOURCES
=========

.. include:: common/resources.rst


FLUX_RFC
========

:doc:`rfc:spec_6`

:doc:`rfc:spec_3`


SEE ALSO
========

:man3:`flux_rpc`, :man3:`flux_rpc_raw`
