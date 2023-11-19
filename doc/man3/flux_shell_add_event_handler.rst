===============================
flux_shell_add_event_handler(3)
===============================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   int flux_shell_add_event_handler (flux_shell_t *shell,
                                     const char *subtopic,
                                     flux_msg_handler_f cb,
                                     void *arg);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

When the shell initializes, it subscribes to all events with the
substring ``shell-JOBID.``, where ``JOBID`` is the jobid under which the
shell is running. :func:`flux_shell_add_event_handler` registers a handler
to be run for a :var:`subtopic` within the shell's event namespace, e.g.
registering a handler for :var:`subtopic` ``"kill"`` will invoke the handler
:var:`cb` whenever an event named ``shell-JOBID.kill`` is generated.


RETURN VALUE
============

Returns -1 if :var:`shell`, :var:`shell->h`, :var:`subtopic` or :var:`cb` are
NULL, or if underlying calls to :linux:man3:`asprintf` or
:man3:`flux_msg_handler_create` fail.


ERRORS
======

EINVAL
   :var:`shell`, :var:`shell->h`, :var:`subtopic` or :var:`cb` are NULL.


RESOURCES
=========

Flux: http://flux-framework.org
