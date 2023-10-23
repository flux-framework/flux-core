====================
flux_shell_getopt(3)
====================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   int flux_shell_getopt (flux_shell_t *shell,
                          const char *name,
                          char **json_str);

   int flux_shell_getopt_unpack (flux_shell_t *shell,
                                 const char *name,
                                 const char *fmt,
                                 ...);

   int flux_shell_setopt (flux_shell_t *shell,
                          const char *name,
                          const char *json_str);

   int flux_shell_setopt_pack (flux_shell_t *shell,
                               const char *name,
                               const char *fmt,
                               ...);


DESCRIPTION
===========

:func:`flux_shell_getopt` gets shell option :var:`name` as a JSON string from
jobspec ``attributes.system.shell.options.name``.

:func:`flux_shell_setopt` sets shell option :var:`name`, making it available to
subsequent calls from :func:`flux_shell_getopt`. If :var:`json_str` is NULL,
the option is unset.

:func:`flux_shell_getopt_unpack` and :func:`flux_shell_setopt_unpack` use
Jansson format strings to accomplish the same functionality.


RETURN VALUE
============

:func:`flux_shell_getopt` and :func:`flux_shell_getopt_unpack` return 1 on
success, 0 if :var:`name` was not set, and -1 on error,

:func:`flux_shell_setopt` and :func:`flux_shell_setopt_pack` return 0 on
success and -1 on error.


ERRORS
======

EINVAL
   :var:`name` or :var:`shell` is NULL.

ENOMEM
   The process has exhausted its memory.


RESOURCES
=========

Flux: http://flux-framework.org
