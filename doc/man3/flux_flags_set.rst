=================
flux_flags_set(3)
=================


SYNOPSIS
========

#include <flux/core.h>

void flux_flags_set (flux_t \*h, int flags);

void flux_flags_unset (flux_t \*h, int flags);

int flux_flags_get (flux_t \*h);


DESCRIPTION
===========

``flux_flags_set()`` sets new open *flags* in handle *h*. The resulting
handle flags will be a logical or of the old flags and the new.

``flux_flags_unset()`` clears open *flags* in handle *h*. The resulting
handle flags will be a logical and of the old flags and the inverse of the new.

``flux_flags_get()`` can be used to retrieve the current open flags from
handle *h*.

The valid flags are described in ``flux_open(3)``.


RETURN VALUE
============

``flux_flags_get()`` returns the current flags.


ERRORS
======

These functions never fail.


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

flux_open(3)
