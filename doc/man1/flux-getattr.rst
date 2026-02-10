===============
flux-getattr(1)
===============


SYNOPSIS
========

| **flux** **getattr** *name*
| **flux** **setattr** [*--force*] *name* *value*
| **flux** **lsattr** [*--values*]


DESCRIPTION
===========

The Flux broker attribute subsystem provides a primitive key-value
configuration mechanism for the broker.  Attributes can be set on the
broker command line with :option:`flux broker --setattr`, then read,
written, or listed using :program:`flux getattr`, :program:`flux setattr`,
or :program:`flux lsattr` after the broker is running.

Attribute scope is local to an individual broker.  That is, broker ranks
may have different values for a given attribute.

:man7:`flux-broker-attributes` provides a catalog of attributes.

COMMANDS
========

getattr
-------

.. program:: flux getattr

:program:`flux getattr` retrieves the value of an attribute.

setattr
-------

.. program:: flux setattr

:program:`flux setattr` assigns a value to an attribute.  If the attribute
does not exist, it is created.

.. option:: -f, --force

  Force an attribute to be set, even if it is not documented to respond
  to runtime updates.


lsattr
------

.. program:: flux lsattr

:program:`flux lsattr` lists attributes.

.. option:: -v, --values

  List the attribute values too.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_attr_get`, :man7:`flux-broker-attributes`
