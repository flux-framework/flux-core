==========================
flux_shell_current_task(3)
==========================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   flux_shell_task_t *flux_shell_current_task (flux_shell_t *shell);

   flux_shell_task_t *flux_shell_task_first (flux_shell_t *shell);

   flux_shell_task_t *flux_shell_task_next (flux_shell_t *shell);


DESCRIPTION
===========

flux_shell_task_first and flux_shell_task_next are used to iterate
over all current tasks known to the shell.

``flux_shell_current_task`` returns the current task for ``task_init``,
``task_exec`` and ``task_exec`` callbacks and NULL in any other
context.

``flux_shell_task_first`` and ``flux_shell_task_next`` return the first
and next tasks, respectively.


RETURN VALUE
============

The relevant ``flux_shell_task_t*`` value, or NULL on error.


ERRORS
======

EINVAL
   ``shell`` is NULL.

EAGAIN
   There are no tasks.


RESOURCES
=========

Flux: http://flux-framework.org
