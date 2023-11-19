===========
flux_log(3)
===========

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  int flux_vlog (flux_t *h, int level, const char *fmt, va_list ap);

  int flux_log (flux_t *h, int level, const char *fmt, ...);

  void flux_log_set_appname (flux_t *h, const char *s);

  void flux_log_set_procid (flux_t *h, const char *s);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_log` creates RFC 5424 format log messages. The log messages
are sent to the Flux message broker on :var:`h` for handling if it is
specified. If :var:`h` is NULL, the log message is output to stderr.

The :var:`level` parameter should be set to one of the :linux:man3:`syslog`
severity levels, which are, in order of decreasing importance:

*LOG_EMERG*
   system is unusable

*LOG_ALERT*
   action must be taken immediately

*LOG_CRIT*
   critical conditions

*LOG_ERR*
   error conditions

*LOG_WARNING*
   warning conditions

*LOG_NOTICE*
   normal, but significant, condition

*LOG_INFO*
   informational message

*LOG_DEBUG*
   debug-level message

When :var:`h` is specified, log messages are are added to the broker's
circular buffer which can be accessed with :man1:`flux-dmesg`. From there,
a message's disposition is up to the broker's log configuration.

:func:`flux_log_set_procid` may be used to override the default procid,
which is initialized to the calling process's PID.

:func:`flux_log_set_appname` may be used to override the default
application name, which is initialized to the value of the :var:`__progname`
symbol (normally the :var:`argv[0]` program name).


MAPPING TO SYSLOG
=================

A Flux log message is formatted as a Flux request with a "raw" payload,
as defined by Flux RFC 3. The raw payload is formatted according to
Internet RFC 5424.

If the Flux handle :var:`h` is specified, the following Syslog header
fields are set in a Flux log messages when it is created within
:func:`flux_log`:

PRI
   Set to the user-specified severity level combined with the facility,
   which is hardwired to *LOG_USER* in Flux log messages.

VERSION
   Set to 1.

TIMESTAMP
   Set to the current UTC wallclock time.

HOSTNAME
   Set to the broker rank associated with *h*.

APP-NAME
   Set to the user-defined application name, truncated to 48 characters,
   excluding terminating NULL.

PROCID
   Set to the PID of the calling process.

MSGID
   Set to the NIL string "-".

The STRUCTURED-DATA portion of the message is empty, and reserved for
future use by Flux.

The MSG portion is post-processed to ensure it contains no NULL's or non-ASCII
characters. At this time non-ASCII UTF-8 is not supported by :func:`flux_log`.


RETURN VALUE
============

:func:`flux_log` normally returns 0 on success, or -1 if there was
a problem building or sending the log message, with :var:`errno` set.


ERRORS
======

EPERM
   The user does not have permission to log messages to this Flux instance.

ENOMEM
   Out of memory.


RESOURCES
=========

Flux: http://flux-framework.org

RFC 5424 The Syslog Protocol: https://tools.ietf.org/html/rfc5424


SEE ALSO
========

:man1:`flux-dmesg`, :man1:`flux-logger`,
