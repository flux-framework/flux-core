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

``flux_shell_getopt()`` gets shell option ``name`` as a JSON string from jobspec
``attributes.system.shell.options.name``.

``flux_shell_setopt()`` sets shell option ``name``, making it available to
subsequent calls from ``flux_shell_getopt()``. If ``json_str`` is NULL,
the option is unset.

``flux_shell_getopt_unpack()`` and ``flux_shell_setopt_unpack()`` use Jansson
format strings to accomplish the same functionality.


RETURN VALUE
============

``flux_shell_getopt()`` and ``flux_shell_getopt_unpack()`` return 1 on success, 0 if ``name`` was not set,
and -1 on error,

``flux_shell_setopt()`` and ``flux_shell_setopt_pack`` return 0 on success and -1 on error.


ERRORS
======

EINVAL
   ``name`` or ``shell`` is NULL.

ENOMEM
   The process has exhausted its memory.


RESOURCES
=========

Flux: http://flux-framework.org
