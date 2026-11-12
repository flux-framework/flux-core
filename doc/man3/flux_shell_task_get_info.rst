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

Link with :command:`-lflux-core`.

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
   "signaled":i,
   "slot":{
     "id":i,
     "label":s,
     "ntasks":i,
     "task_index":i,
     "cores":s,
     "gpus":s
   }

The :var:`slot` key is present only if the scheduler assigned RFC 20 task
slots in :var:`R`. It describes the slot to which the task is assigned:
:var:`id` is the index of the slot in the :var:`slots` array of the
shell rank info (see :man3:`flux_shell_get_rank_info`), :var:`label` is
the slot label, :var:`ntasks` is the number of tasks assigned to the
slot, :var:`task_index` is the position of this task among those tasks
(from 0), and :var:`cores` and :var:`gpus` are RFC 22 idsets of the
resources of the slot. If more tasks than slots are assigned to a rank,
tasks share slots, which a plugin may use :var:`task_index` and
:var:`ntasks` to divide.


RETURN VALUE
============

Returns 0 on success and -1 on failure. A failure will not
necessarily set errno.


ERRORS
======

EINVAL
   If :var:`task` or :var:`json_str` is NULL.


RESOURCES
=========

.. include:: common/resources.rst
