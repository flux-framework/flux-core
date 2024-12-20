======================
flux_shell_get_info(3)
======================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   int flux_shell_get_info (flux_shell_t *shell,
                           char **json_str);

   int flux_shell_info_unpack (flux_shell_t *shell,
                              const char *fmt,
                              ...);

   int flux_shell_get_rank_info (flux_shell_t *shell,
                                 int shell_rank,
                                 char **json_str);

   int flux_shell_rank_info_unpack (flux_shell_t *shell,
                                    int shell_rank,
                                    const char *fmt,
                                    ...);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_shell_get_info` returns shell information as a json string
with the following layout:

::

   "jobid":I,
   "instance_owner":i,
   "rank":i,
   "size":i,
   "ntasks";i,
   "service";s,
   "options": { "verbose":b, "standalone":b },
   "jobspec":o,
   "R":o

:func:`flux_shell_get_rank_info` returns shell rank information as a json
string with the following layout:

::

   "id":i,
   "name":s,
   "broker_rank":i,
   "ntasks":i
   "taskids":s
   "resources": { "ncores":i, "cores":s, ... }

where :var:`id` is the shell rank, :var:`name` is the hostname of that shell
rank, :var:`broker_rank` is the broker rank on which the target shell rank
of the query is running, :var:`ntasks` is the number of tasks running under
that shell rank, :var:`taskids` is a list of task id assignments for those
tasks (an RFC 22 idset string), and :var:`resources` is a dictionary of
resource name to resource ids assigned to the shell rank.

:func:`flux_shell_info_unpack` and :func:`flux_shell_rank_info_unpack`
accomplished the same thing with Jansson-style formatting arguments.

If :var:`shell_rank` is set to -1, the current shell rank is used.


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
