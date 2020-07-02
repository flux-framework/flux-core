======================
flux_shell_get_info(3)
======================


SYNOPSIS
========

::

   #include <flux/shell.h>
   #include <errno.h>

::

   int flux_shell_get_info (flux_shell_t *shell,
                           char **json_str);

::

   int flux_shell_info_unpack (flux_shell_t *shell,
                              const char *fmt,
                              ...);

::

   int flux_shell_get_rank_info (flux_shell_t *shell,
                                 int shell_rank,
                                 char **json_str);

::

   int flux_shell_rank_info_unpack (flux_shell_t *shell,
                                    int shell_rank,
                                    const char *fmt,
                                    ...);


DESCRIPTION
===========

``flux_shell_get_info()`` returns shell information as a json string
with the following layout:

::

   "jobid":I,
   "rank":i,
   "size":i,
   "ntasks";i,
   "service";s,
   "options": { "verbose":b, "standalone":b },
   "jobspec":o,
   "R":o

``flux_shell_get_rank_info()`` returns shell rank information as a json
string with the following layout:

::

   "broker_rank":i,
   "ntasks":i
   "resources": { "cores":s, ... }

``flux_shell_info_unpack()`` and ``flux_shell_rank_info_unpack()``
accomplished the same thing with Jansson-style formatting arguments.

If ``shell_rank`` is set to -1, the current shell rank is used.


RETURN VALUE
============

All functions return 0 on success and -1 on error.


ERRORS
======

EINVAL
   if ``shell`` is NULL, or either ``json_str`` or ``fmt`` are NULL, or if
   ``shell_rank`` is less than -1.


SEE ALSO
========

For an overview of the Jansson API, see https://jansson.readthedocs.io/en/2.8/apiref.html.


RESOURCES
=========

Github: http://github.com/flux-framework
