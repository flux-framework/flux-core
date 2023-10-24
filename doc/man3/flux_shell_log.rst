=================
flux_shell_log(3)
=================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   void flux_shell_log (const char *component,
                        int level,
                        const char *file,
                        int line,
                        const char *fmt,
                        ...);

   int flux_shell_err (const char *component,
                       const char *file,
                       int line,
                       int errnum,
                       const char *fmt,
                       ...);

   void flux_shell_fatal (const char *component,
                          const char *file,
                          int line,
                          int errnum,
                          int exit_code,
                          const char *fmt,
                          ...);

   void flux_shell_raise (const char *type,
                          int severity,
                          const char *fmt,
                          ...);

   int flux_shell_log_setlevel (int level, const char *dest);


DESCRIPTION
===========

:func:`flux_shell_log` logs a message at for shell component or plugin
:var:`component` at :var:`level` to all loggers registered to receive messages
at that severity or greater. See :man3:`flux_log` for a list of supported
levels.


The following macros handle common levels. For external shell plugins,
the required macro ``FLUX_SHELL_PLUGIN_NAME`` is automatically substituted
for the :var:`component` in all macros.


::

   #define shell_trace(...) \

::

   #define shell_debug(...) \

::

   #define shell_log(...) \

::

   #define shell_warn(...) \

::

   #define shell_log_error(...) \

:func:`flux_shell_err` logs a message at FLUX_SHELL_ERROR level,
additionally appending the result of strerror(``errnum``) for
convenience. Macros include:

::

   #define shell_log_errn(errn, ...) \

::

   #define shell_log_errno(...) \

Note that :var:`errno` is the standard global value defined in ``errno.h``
and :var:`errn` is a user-provided error code.

func:`flux_shell_fatal` logs a message at FLUX_SHELL_FATAL level and
schedules termination of the job shell. This may generate an
exception if tasks are already running. Exits with :var:`exit_code`.
While the macro names are similar to those using :func:`flux_shell_err`,
note that the choices of :var:`errnum` are either 0 or :var:`errno`.

::

   #define shell_die(code,...) \

::

   #define shell_die_errno(code,...) \

:func:`flux_shell_raise` explicitly raises an exception for the current
job of the given :var:`type` and :var:`severity`. Exceptions of severity 0
will result in termination of the job by the execution system.

:func:`flux_shell_log_setlevel` sets default severity of logging
destination :var:`dest` to :var:`level`. If :var:`dest` is NULL then the
internal log dispatch level is set (i.e. no messages above severity level will
be logged to any log destination). Macros include:

::

   #define shell_set_verbose(n) \
   flux_shell_log_setlevel(FLUX_SHELL_NOTICE+n, NULL)

::

   #define shell_set_quiet(n) \
   flux_shell_log_setlevel(FLUX_SHELL_NOTICE-n, NULL)

As a special case, if :var:`level` is set to ``FLUX_SHELL_QUIET``, then
logging will be completely disabled to :var:`dest`. For example, to disable
logging to :var:`stderr`, use:

::

   flux_shell_log_setlevel (FLUX_SHELL_QUIET, "stderr");


RETURN VALUE
============

:func:`flux_shell_err` returns -1 with :var:`errno` = :var:`errnum`, so that the
function can be used as:
return flux_shell_err(…​);

:func:`flux_shell_log_setlevel` will return -1 and set :var:`errno` to EINVAL
if the requested :var:`level` is not valid or if :var:`dest` is not a valid
pointer to a logger shell.


ERRORS:
=======

EINVAL
   :var:`level` or :var:`dest` is not valid.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man3:`flux_log`
