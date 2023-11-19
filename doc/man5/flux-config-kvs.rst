==================
flux-config-kvs(5)
==================


DESCRIPTION
===========

The Flux system instance **kvs** service provides the primary key value
store (i.e. "the KVS") for a large number of Flux services.  For
example, job eventlogs are stored in the KVS.

The ``kvs`` table may contain the following keys:


KEYS
====

checkpoint-period
   (optional) Sets a period of time (in RFC 23 Flux Standard Duration
   format) that the KVS will regularly checkpoint a reference to its
   primary namespace.  The checkpoint is used to protect against data
   loss in the event of a Flux broker crash.

gc-threshold
   (optional) Sets the number of KVS commits (distinct root snapshots)
   after which offline garbage collection is performed by
   :man1:`flux-shutdown`. A value of 100000 may be a good starting
   point. (Default: garbage collection must be manually requested with
   `flux-shutdown --gc`).


EXAMPLE
=======

::

   [kvs]
   checkpoint-period = "30m"
   gc-threshold = 100000

RESOURCES
=========

.. include:: common/resources.rst

RFC 23: Flux Standard Duration: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_23.html


SEE ALSO
========

:man1:`flux-shutdown`,:man5:`flux-config`
