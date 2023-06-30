====================
flux_shell_getenv(3)
====================


SYNOPSIS
========

::

   #include <flux/shell.h>
   #include <errno.h>

::

   const char * flux_shell_getenv (flux_shell_t *shell,
                                   const char *name);

::

   int flux_shell_get_environ (flux_shell_t *shell,
                               char **json_str);

::

   int flux_shell_setenvf (flux_shell_t *shell,
                           int overwrite,
                           const char *name,
                           const char *fmt,
                           ...)

::

   int flux_shell_unsetenv (flux_shell_t *shell,
                            const char *name);


DESCRIPTION
===========

``flux_shell_getenv()`` returns the value of an environment variable from the global job environment.
``flux_shell_get_environ()`` returns 0 on success with ``*json_str`` set
to an allocated JSON string, or -1 on failure with ``errno`` set.
``flux_shell_setenvf()`` sets an environment variable in the global job
environment using :linux:man3:`printf` style format arguments.
``flux_shell_unsetenv()`` unsets the specified environment variable in the global job environment.


RETURN VALUE
============

``flux_shell_getenv()`` returns NULL if either ``shell`` or ``name`` is NULL, or if the variable is not found.

``flux_shell_get_environ()`` returns a json string on success or NULL on failure.

``flux_shell_setenvf()`` and ``flux_shell_unsetenv()`` return 0 on success and -1 on failure.


ERRORS
======

EINVAL
   ``shell``, ``name`` or ``fmt`` is NULL.

EEXIST
   The variable already exists and ``overwrite`` was not non-zero (``flux_shell_setenvf()``).

ENOENT
   With ``flux_shell_unsetenv()``, the target variable does not exist.

RESOURCES
=========

Flux: http://flux-framework.org
