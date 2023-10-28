==============
flux-logger(1)
==============


SYNOPSIS
========

**flux** **logger** [*--severity SEVERITY*] [*--appname NAME*] *message* *...*

DESCRIPTION
===========

flux-logger(1) appends Flux log entries to the local Flux
broker's circular buffer.

Log entries are associated with a :linux:man3:`syslog` style severity.
Valid severity names are *emerg*, *alert*, *crit*, *err*,
*warning*, *notice*, *info*, *debug*.

Log entries may also have a user-defined application name.
This is different than the syslog *facility*, which is always set
to LOG_USER in Flux log messages.

The wall clock time (UTC) and the broker rank are added to the log
message when it is created.


OPTIONS
=======

.. option:: -s, --severity=SEVERITY

   Specify the log message severity. The default severity is *info*.

.. option:: -n, --appname=NAME

   Specify a user-defined application name to associate with the log message.
   The default appname is *logger*.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man1:`flux-dmesg`, :man3:`flux_log`, :linux:man3:`syslog`
