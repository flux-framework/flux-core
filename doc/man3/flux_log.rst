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

:func:`flux_log` sends log messages to the Flux broker connected to :var:`h`.
If :var:`h` is NULL, the log message is output to stderr.  The broker log
service is described in the :ref:`broker_logging` section of
:man1:`flux-broker`.

The :var:`level` parameter should be set to one of the :linux:man3:`syslog`
severity levels.  The levels are listed in the aforementioned description.

:func:`flux_log_set_procid` may be used to override the default procid,
which is initialized to the calling process's PID.

:func:`flux_log_set_appname` may be used to override the default
application name, which is initialized to the value of the :var:`__progname`
symbol (normally the :var:`argv[0]` program name).


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

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-broker`, :man1:`flux-dmesg`, :man1:`flux-logger`,
