=================
flux_flags_set(3)
=================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  void flux_flags_set (flux_t *h, int flags);

  void flux_flags_unset (flux_t *h, int flags);

  int flux_flags_get (flux_t *h);


DESCRIPTION
===========

:func:`flux_flags_set` sets new open *flags* in handle *h*. The resulting
handle flags will be a logical or of the old flags and the new.

:func:`flux_flags_unset` clears open *flags* in handle *h*. The resulting
handle flags will be a logical and of the old flags and the inverse of the new.

:func:`flux_flags_get` can be used to retrieve the current open flags from
handle *h*.

The valid flags are described in :man3:`flux_open`.


RETURN VALUE
============

:func:`flux_flags_get` returns the current flags.


ERRORS
======

These functions never fail.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_open`
