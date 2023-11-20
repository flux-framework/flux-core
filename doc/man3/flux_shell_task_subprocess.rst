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

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_shell_task_subprocess` returns the subprocess for a shell
task in var:`task_fork` and :var:`task_exit` callbacks.

:func:`flux_shell_task_cmd` returns the cmd structure for a shell task.


RETURN VALUE
============

:func:`flux_shell_task_subprocess` returns the :var:`proc` field of the
:var:`task`, and :func:`flux_shell_task_cmd` returns the :var:`cmd` field,
or NULL on error.


ERRORS
======

EINVAL
   :var:`task` is NULL.


RESOURCES
=========

.. include:: common/resources.rst
