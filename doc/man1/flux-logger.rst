==============
flux-logger(1)
==============


SYNOPSIS
========

**flux** **logger** [*--severity SEVERITY*] [*--appname NAME*] *message* *...*

DESCRIPTION
===========

.. program:: flux logger

:program:`flux logger` sends a log message to the Flux log service.
For more information, refer to the :ref:`broker_logging` section of
:man1:`flux-broker`.

The wall clock time (UTC) and the broker rank are added to the log
message when it is created.


OPTIONS
=======

.. option:: -s, --severity=SEVERITY

   Specify the log message severity by name.  Valid severity names are
   *emerg*, *alert*, *crit*, *err*, *warning*, *notice*, *info*, *debug*.
   The default severity is *info*.

.. option:: -n, --appname=NAME

   Log entries may have a user-defined application name.
   The default appname is *logger*.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-broker`, :man1:`flux-dmesg`, :man3:`flux_log`, :linux:man3:`syslog`
