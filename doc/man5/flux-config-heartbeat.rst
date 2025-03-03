========================
flux-config-heartbeat(5)
========================


DESCRIPTION
===========

The ``heartbeat`` table may be used to tune the configuration of the
Flux heartbeat module, which publishes periodic ``heartbeat.pulse`` messages
for synchronization.

It may contain the following keys:


KEYS
====

period
   (optional) The interval (in RFC 23 Flux Standard Duration format) between
   the publication of heartbeat messages.  Default: ``"2s"``.


EXAMPLE
=======

::

   [heartbeat]
   period = "5s"


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_23`


SEE ALSO
========

:man5:`flux-config`
