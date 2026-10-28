==================
flux_msg_encode(3)
==================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  ssize_t flux_msg_encode_size (const flux_msg_t *msg);

  int flux_msg_encode (const flux_msg_t *msg,
                       void *buf,
                       size_t size);

  flux_msg_t *flux_msg_decode (const void *buf, size_t size);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_msg_encode_size` returns the number of bytes required to
hold the serialized representation of :var:`msg`.

:func:`flux_msg_encode` converts :var:`msg` to a serialized representation,
storing it in the caller-provided buffer :var:`buf` of :var:`size` bytes.
The buffer must be at least as large as the value returned by
:func:`flux_msg_encode_size`.

:func:`flux_msg_decode` performs the inverse, creating :var:`msg` from
:var:`buf` and :var:`size`.  The caller must destroy :var:`msg` with
:func:`flux_msg_destroy`.


RETURN VALUE
============

:func:`flux_msg_encode_size` returns the required buffer size on success.
On error, -1 is returned, and :var:`errno` is set appropriately.

:func:`flux_msg_encode` returns 0 on success. On error, -1 is returned,
and :var:`errno` is set appropriately.

:func:`flux_msg_decode` the decoded message on success. On error, NULL
is returned, and :var:`errno` is set appropriately.


ERRORS
======

EINVAL
   Some arguments were invalid.

ENOMEM
   Out of memory.


RESOURCES
=========

.. include:: common/resources.rst
