=============================
flux_shell_mustache_render(3)
=============================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>

   char *flux_shell_mustache_render (flux_shell_t *shell,
                                     const char *fmt);

   char *flux_shell_rank_mustache_render (flux_shell_t *shell,
                                          int shell_rank,
                                          const char *fmt);

   char *flux_shell_task_mustache_render (flux_shell_t *shell,
                                          flux_shell_task_t *task,
                                          const char *fmt);

DESCRIPTION
===========

:func:`flux_shell_mustache_render` expands mustache templates in :var:`fmt`
using the current shell context and returns an allocated string with the
result. The caller must free the result.

:func:`flux_shell_rank_mustache_render` is similar but renders the template
for an alternate shell rank specified by :var:`shell_rank`.

:func:`flux_shell_task_mustache_render` renders the template for a specific
task specified by :var:`task`.

These functions are useful for expanding job-specific templates in environment
variables, command arguments, or plugin configurations.

RETURN VALUE
============

These functions return an allocated string on success which the caller must
free, or NULL on failure with :var:`errno` set.


ERRORS
======

EINVAL
   :var:`shell`, :var:`fmt`, or :var:`task` is NULL.

ENOMEM
   Out of memory.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man7:`flux-shell-plugins`
