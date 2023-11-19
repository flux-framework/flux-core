==================
flux_msg_encode(3)
==================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  int flux_msg_encode (const flux_msg_t *msg,
                       void **buf,
                       size_t *size);

  flux_msg_t *flux_msg_decode (void *buf, size_t size);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_msg_encode` converts :var:`msg` to a serialized representation,
allocated internally and assigned to :var:`buf`, number of bytes to :var:`size`.
The caller must release :var:`buf` with :linux:man3:`free`.

:func:`flux_msg_decode` performs the inverse, creating :var:`msg` from
:var:`buf` and :var:`size`.  The caller must destroy :var:`msg` with
:func:`flux_msg_destroy`.


RETURN VALUE
============

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

Flux: http://flux-framework.org
