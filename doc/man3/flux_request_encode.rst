======================
flux_request_encode(3)
======================


SYNOPSIS
========

::

   #include <flux/core.h>

::

   flux_msg_t *flux_request_encode (const char *topic,
                                    const char *s);

::

   flux_msg_t *flux_request_encode_raw (const char *topic,
                                        void *data, int len);


DESCRIPTION
===========

``flux_request_encode()`` encodes a request message with topic string
*topic* and optional NULL terminated string payload *s*. The newly constructed
message that is returned must be destroyed with ``flux_msg_destroy()``.

``flux_request_encode_raw()`` encodes a request message with topic
string *topic*. If *data* is non-NULL its contents will be used
as the message payload, and the payload type set to raw.


RETURN VALUE
============

These functions return a message on success. On error, NULL is
returned, and errno is set appropriately.


ERRORS
======

EINVAL
   The *topic* argument was NULL or *s* is not NULL terminated.

ENOMEM
   Memory was unavailable.


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

flux_response_decode(3), flux_rpc(3)
