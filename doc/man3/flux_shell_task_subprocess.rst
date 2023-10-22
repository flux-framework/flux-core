=============================
flux_shell_task_subprocess(3)
=============================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>

   flux_subprocess_t *flux_shell_task_subprocess (flux_shell_task_t *task);

   flux_cmd_t *flux_shell_task_cmd (flux_shell_task_t *task);


DESCRIPTION
===========

:func:`flux_shell_task_subprocess` returns the subprocess for a shell
task in ``task_fork`` and ``task_exit`` callbacks.

:func:`flux_shell_task_cmd` returns the cmd structure for a shell task.


RETURN VALUE
============

:func:`flux_shell_task_subprocess` returns the ``proc`` field of the
``task``, and :func:`flux_shell_task_cmd` returns the ``cmd`` field,
or NULL on error.


ERRORS
======

EINVAL
   ``task`` is NULL.


RESOURCES
=========

Flux: http://flux-framework.org
