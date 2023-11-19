======================
flux-config-archive(5)
======================


DESCRIPTION
===========

The **job-archive** service periodically archives job data in a
sqlite database for use by **flux-accounting**.  Parameters may
be set by the ``archive`` table which may contain the following keys:


KEYS
====

period
   (required) Set the archival period (in RFC 23 Flux Standard Duration format).

dbpath
   (optional) Set the path to the sqlite database file.  The service does
   nothing if this key is unset.

busytimeout
   (optional) Set the sqlite busy_timeout pragma (in RFC 23 Flux Standard
   Duration format).  The default is 50s.


EXAMPLE
=======

::

   [archive]
   dbpath = "/var/lib/flux/job-archive.sqlite"
   period = "1m"
   busytimeout = "50s"


RESOURCES
=========

.. include:: common/resources.rst

RFC 23: Flux Standard Duration: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_23.html


SEE ALSO
========

:man5:`flux-config`
