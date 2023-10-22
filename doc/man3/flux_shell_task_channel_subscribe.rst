====================================
flux_shell_task_channel_subscribe(3)
====================================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>

   int flux_shell_task_channel_subscribe (flux_shell_task_t *task,
                                          const char *channel,
                                          flux_shell_task_io_f cb,
                                          void *arg);


DESCRIPTION
===========

Call ``cb`` when shell task output channel ``name`` is ready for reading.

Callback can then call :man3:`flux_shell_task_get_subprocess` and use
:man3:`flux_subprocess_read` or :man3:`flux_subprocess_getline` on the
result to get available data. Only one subscriber per stream is allowed.


RETURN VALUE
============

Returns 0 on success and -1 on error.

Not yet implemented.


ERRORS
======

EEXIST
   :func:`flux_shell_task_channel_subscribe` is called on a stream with an
   existing subscriber


RESOURCES
=========

Flux: http://flux-framework.org
