====================
flux_shell_getenv(3)
====================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   const char * flux_shell_getenv (flux_shell_t *shell,
                                   const char *name);

   int flux_shell_get_environ (flux_shell_t *shell,
                               char **json_str);

   int flux_shell_setenvf (flux_shell_t *shell,
                           int overwrite,
                           const char *name,
                           const char *fmt,
                           ...);

   int flux_shell_unsetenv (flux_shell_t *shell,
                            const char *name);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_shell_getenv` returns the value of an environment variable from
the global job environment.  :func:`flux_shell_get_environ` returns 0 on
success with :var:`json_str` set to an allocated JSON string, or -1 on failure
with :var:`errno` set.  :func:`flux_shell_setenvf` sets an environment variable
in the global job environment using :linux:man3:`printf` style format
arguments.  :func:`flux_shell_unsetenv` unsets the specified environment
variable in the global job environment.


RETURN VALUE
============

:func:`flux_shell_getenv` returns NULL if either :var:`shell` or :var:`name`
is NULL, or if the variable is not found.

:func:`flux_shell_get_environ` returns a json string on success or NULL on
failure.

:func:`flux_shell_setenvf` and :func:`flux_shell_unsetenv` return 0 on
success and -1 on failure.


ERRORS
======

EINVAL
   :var:`shell`, :var:`name` or :var:`fmt` is NULL.

EEXIST
   The variable already exists and :var:`overwrite` was not non-zero
   (func:`flux_shell_setenvf`).

ENOENT
   With :func:`flux_shell_unsetenv`, the target variable does not exist.

RESOURCES
=========

Flux: http://flux-framework.org
