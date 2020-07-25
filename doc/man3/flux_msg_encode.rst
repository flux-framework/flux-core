==================
flux_msg_encode(3)
==================


SYNOPSIS
========

#include <flux/core.h>

int flux_msg_encode (const flux_msg_t \*msg, void \**buf, size_t \*size);

flux_msg_t \*flux_msg_decode (void \*buf, size_t size);


DESCRIPTION
===========

``flux_msg_encode()`` converts *msg* to a serialized representation,
allocated internally and assigned to *buf*, number of bytes to *size*.
The caller must release *buf* with free(3).

``flux_msg_decode()`` performs the inverse, creating *msg* from *buf* and *size*.
The caller must destroy *msg* with flux_msg_destroy().


RETURN VALUE
============

``flux_msg_encode()`` returns 0 on success. On error, -1 is returned,
and errno is set appropriately.

``flux_msg_decode()`` the decoded message on success. On error, NULL
is returned, and errno is set appropriately.


ERRORS
======

EINVAL
   Some arguments were invalid.

ENOMEM
   Out of memory.


RESOURCES
=========

Github: http://github.com/flux-framework
