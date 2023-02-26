===============
flux-getattr(1)
===============


SYNOPSIS
========

**flux** **getattr** *name*

**flux** **setattr** *name* *value*

**flux** **setattr** *name*

**flux** **lsattr** [*--values*]


DESCRIPTION
===========

Flux broker attributes are both a simple, general-purpose key-value
store with scope limited to the local broker rank, and a method for the
broker to export information needed by Flux services and utilities.

flux-getattr(1) retrieves the value of an attribute.

flux-setattr(1) assigns a new value to an attribute, or optionally
removes an attribute.

flux-lsattr(1) lists attribute names, optionally with their values.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_attr_get`, :man7:`flux-broker-attributes`
