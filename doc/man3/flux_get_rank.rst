================
flux_get_rank(3)
================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  int flux_get_rank (flux_t *h, uint32_t *rank);

  int flux_get_size (flux_t *h, uint32_t *size);


DESCRIPTION
===========

``flux_get_rank()`` and ``flux_get_size()`` ask the
Flux broker for its rank in the Flux instance, and the size of the Flux
instance.

Ranks are numbered 0 through size - 1.


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

.. literalinclude:: example/info.c
  :language: c


RESOURCES
=========

Flux: http://flux-framework.org

