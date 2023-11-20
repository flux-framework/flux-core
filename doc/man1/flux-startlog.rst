================
flux-startlog(1)
================


SYNOPSIS
========

**flux** **startlog**


DESCRIPTION
===========

.. program:: flux startlog

List the Flux instance's start and stop times, by interpreting the contents
of the KVS ``admin.eventlog``.

A ``start`` event is posted to the eventlog at startup, and a ``finish`` event
is posted to the eventlog at finalization.  The timestamps on the two events
are used to calculate the instance run time, which is shown in the listing
in Flux Standard Duration format.

If the current ``start`` event is not immediately preceded by a ``finish``
event (unless it is the first entry in the eventlog), then the Flux instance
may have crashed and data may have been lost.  If this is detected on instance
startup, it is logged by the broker's ``rc1`` script on the next reboot.

This command is not available to guest users.


OPTIONS
=======

.. option:: -h, --help

   Summarize available options.

.. option:: --check

   If the instance has most recently restarted from a crash, exit with a
   return code of 1, otherwise 0.

.. option:: --quiet

   Suppress non-error output.

.. option:: -v, --show-version

   Show the flux-core software version associated with each start event.


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_18`

:doc:`rfc:spec_23`


SEE ALSO
========

:man1:`flux-uptime`, :man1:`flux-kvs`
