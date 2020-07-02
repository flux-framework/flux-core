================
flux_get_rank(3)
================


SYNOPSIS
========

#include <flux/core.h>

int flux_get_rank (flux_t \*h, uint32_t \*rank);

int flux_get_size (flux_t \*h, uint32_t \*size);


DESCRIPTION
===========

``flux_get_rank()`` and ``flux_get_size()`` ask the
Flux broker for its rank in the comms session, and the size of the comms
session.

Session ranks are numbered 0 through size - 1.


RETURN VALUE
============

These functions return zero on success. On error, -1 is returned, and errno
is set appropriately.


ERRORS
======

EINVAL
   Some arguments were invalid.


EXAMPLES
========

Example:

.. literalinclude:: tinfo.c


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

`RFC 3: CMB1 - Flux Comms Message Broker Protocol <https://github.com/flux-framework/rfc/blob/master/spec_3.rst>`__
