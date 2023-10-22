===========================
flux_shell_task_get_info(3)
===========================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>

   int flux_shell_task_get_info (flux_shell_task_t *task,
                                 char **json_str);

   int flux_shell_task_info_unpack (flux_shell_task_t *task,
                                    const char *fmt,
                                    ...);


DESCRIPTION
===========

Returns task info either as a json string (specified below) or
using Jansson-style parameters. The structure of the former is:

::

   "localid":i,
   "rank":i,
   "state":s,
   "pid":I,
   "wait_status":i,
   "exitcode":i,
   "signaled":i


RETURN VALUE
============

Returns 0 on success and -1 on failure. A failure will not
necessarily set errno.


ERRORS
======

EINVAL
   If ``task`` or ``json_str`` is NULL.


RESOURCES
=========

Flux: http://flux-framework.org
