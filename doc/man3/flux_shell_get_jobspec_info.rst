==============================
flux_shell_get_jobspec_info(3)
==============================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   int flux_shell_get_jobspec_info (flux_shell_t *shell,
                                    char **json_str);

   int flux_shell_jobspec_info_unpack (flux_shell_t *shell,
                                       const char *fmt,
                                       ...);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_shell_get_jobspec_info` returns jobspec summary information
from the flux job shell as a json string. The only key guaranteed to
be in the returned JSON object is the jobspec :var:`version`, e.g.

::
   {"version": 1}


For jobspec version 1, the following keys are also available:

::

   {
     "ntasks":i,         # number of tasks requested
     "nslots":i,         # number of task slots
     "cores_per_slot":i  # number of cores per task slot
     "gpus_per_slot":i   # number of gpus per task slot
     "nnodes":i          # number of nodes requested, -1 if unset
     "slots_per_node":i  # number of slots per node, -1 if unavailable
     "node_exclusive":b  # true if exclusive=true set on node resource
    }

This summary information is derived from the jobspec by the shell and
is shared with plugins in order to avoid duplication of effort.

Currently only version 1 jobspec is supported.

:func:`flux_shell_jobspec_info_unpack` accomplishes the same thing with
Jansson-style formatting arguments.


RETURN VALUE
============

All functions return 0 on success and -1 on error.


ERRORS
======

EINVAL
   if :var:`shell` is NULL, or either :var:`json_str` or :var:`fmt` are NULL,
   or if :var:`shell_rank` is less than -1.


RESOURCES
=========

.. include:: common/resources.rst

Jansson: https://jansson.readthedocs.io/en/2.9/apiref.html
