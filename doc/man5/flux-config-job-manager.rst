==========================
flux-config-job-manager(5)
==========================


DESCRIPTION
===========

The Flux **job-manager** service may be configured via the ``job-manager``
table, which may contain the following keys:

Note that this set of keys may be extended by loaded jobtap plugins.
For details pertaining to some plugins distributed with flux see
:ref:`plugin_specific_keys` below.


KEYS
====

inactive-age-limit
   (optional) String (in RFC 23 Flux Standard Duration format) that specifies
   the maximum age of inactive jobs retained in the KVS.  The age is computed
   since the job became inactive.  Once a job is removed from the KVS, its job
   data is not longer available.  Inactive jobs can also be manually purged
   with :man1:`flux-job` ``purge``.

inactive-num-limit
   (optional) Integer maximum number of inactive jobs retained in the KVS.

stop-queues-on-restart
   (optional) Boolean value indicating if the job manager should automatically
   stop any started queues during a restart. Queues stopped in this manner will
   have their stop reason set to ::

      Automatically stopped due to restart

   while queues stopped before a shutdown will remain stopped with their
   original stop reason. The default value is ``false``, which means that
   started queues will remain started upon restart.

plugins
   (optional) An array of objects defining a list of jobtap plugin directives.
   Each directive follows the format defined in the :ref:`plugin_directive`
   section.

housekeeping
   (optional) Table of configuration for the job-manager housekeeping
   service. The housekeeping service is an alternative for
   handling administrative job epilog workloads. If enabled, resources are
   released by jobs to housekeeping, which runs a command or a systemd unit
   and releases resources to the scheduler on completion. See configuration
   details in the :ref:`housekeeping` section.

   Note: The housekeeping script runs as the instance owner (e.g. "flux").
   On a real system, "command" is configured to "imp run housekeeping",
   and the IMP is configured to launch the flux-housekeeping systemd
   service as root. (See :man5:`flux-config-security-imp` for details
   on configuring :command:`flux imp run`).


.. _plugin_directive:

PLUGIN DIRECTIVE
================

load
   (optional) A string instructing the job manager to load a plugin matching
   the given filename into the job-manager.  If the path is not absolute,
   then the first plugin matching the job-manager searchpath will be loaded.

remove
   (optional) A string instructing the job manager to remove all plugins
   matching  the  value.  The  value may be a :linux:man7:`glob`. If ``remove``
   appears with ``load``, plugin removal is always handled first.  The special
   value ``all`` is a synonym for ``*``, but will not fail when no plugins
   match.

conf
   (optional) An object, valid with ``load`` only, that defines a configuration
   table to pass to the loaded plugin.

.. _housekeeping:

HOUSEKEEPING
============

command
  (optional) An array of strings specifying the housekeeping command.
  If unspecified but the housekeeping table exists, then assume the command is
  ``imp run housekeeping``.

release-after
  (optional) A string specified in Flux Standard Duration (FSD). If unset,
  resources for a given job are not released until all execution targets for
  a given job have completed housekeeping. If set to ``0``, resources are
  released as each target completes. Otherwise, a timer is started when the
  first execution target for a given job completes, and all resources that
  have completed housekeeping when the timer fires are released. Following
  that, resources are released as each execution target completes.

.. _plugin_specific_keys:

PLUGIN SPECIFIC KEYS
====================

The following configuration keys are supported by included jobtap plugins.
The documented config will have no effect if the associated plugin is not
loaded into the job-manager.

perilog.so
----------

The ``perilog.so`` plugin supports configuration and execution of job-manager
prolog and/or epilog. The following keys are supported when the ``perilog.so``
plugin is loaded:

prolog
   (optional) Table of configuration for a job-manager prolog. If enabled,
   the prolog is initiated when the job enters the RUN state before any
   job shells (i.e. user processes) are started. The following keys are
   supported for the ``[job-manager.prolog]`` table:

   command
      (optional, string) An array of strings specifying the command to run. If
      ``exec.imp`` is set, the the default command is ``["flux-imp",
      "run", "prolog"]``, otherwise it is an error if command is not set.
   per-rank
      (optional, bool) By default the job-manager prolog only runs ``command``
      on rank 0. With ``per-rank=true``, the command will be run on each
      rank assigned to the job.
   timeout
      (optional, string) A string value in Flux Standard Duration specifying a
      timeout for the prolog, after which it is terminated (and a job
      exception raised). The default prolog timeout is 30m. To disable
      the timeout use ``0`` or ``infinity``.
   kill-timeout
      (optional, float) If the prolog times out, or a job exception is raised
      during the job prolog and ``cancel-on-exception`` is true, the prolog
      will be canceled by sending it a SIGTERM signal. ``kill-timeout``
      is a floating point number of seconds to wait until any nodes with
      prolog tasks that are still active will be drained. The drain reason
      will include the string "timed out" if the prolog timeout was reached,
      or "canceled then timed out" if the prolog was canceled after a job
      exception then timed out. The default is 60.
   cancel-on-exception
      (optional, bool) A boolean indicating if the prolog should be terminated
      when a fatal job exception is raised while the prolog is active. The
      default is true.

epilog
   (optional) Table of configuration for a job-manager epilog. If
   configured, the epilog is started at the job ``finish`` event,
   i.e. after all user processes and job shells have terminated, or after
   prolog failure (in which case there will not be a job ``finish`` event.)
   The ``[job-manager.epilog]`` table supports the following keys:

   command
      (optional, string) An array of strings specifying the command to run. If
      ``exec.imp`` is set, the the default command is ``["flux-imp",
      "run", "prolog"]``, otherwise it is an error if command is not set.
   per-rank
      (optional, bool) By default the job-manager epilog only runs ``command``
      on rank 0. With ``per-rank=true``, the command will be run on each
      rank assigned to the job.
   timeout
      (optional, string) A string value in Flux Standard Duration specifying a
      timeout for the epilog, after which it is terminated (and a job
      exception raised). By default, the epilog timeout is disabled.
   kill-timeout
      (optional, float) If the epilog times out, or a job exception is raised
      during the job epilog and ``cancel-on-exception`` is true, the epilog
      will be canceled by sending it a SIGTERM signal. ``kill-timeout``
      is a floating point number of seconds to wait until any nodes with
      epilog tasks that are still active will be drained. The drain reason
      will include the string "timed out" if the epilog timeout was reached,
      or "canceled then timed out" if the epilog was canceled after a job
      exception then timed out. The default is 60.
   cancel-on-exception
      (optional, bool) A boolean indicating if the epilog should be terminated
      when a fatal job exception is raised while the epilog is active. The
      default is false. (``cancel-on-exception`` should only be used with
      the epilog for testing purposes, since users can generate exceptions
      on their jobs)

perilog
  (optional) Common prolog/epilog configuration keys:

   log-ignore
      (optional) An array of regular expression strings to ignore in the
      stdout and stderr of prolog and epilog processes.


EXAMPLE
=======

::

   [job-manager]

   journal-size-limit = 10000

   inactive-age-limit = "7d"
   inactive-num-limit = 10000

   plugins = [
      {
        load = "priority-custom.so",
        conf = {
           job-limit = 100,
           size-limit = 128
        }
      }
   ]

   [job-manager.housekeeping]
   release-after = "1m"


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man5:`flux-config`, :man1:`flux-jobtap`, :man7:`flux-jobtap-plugins`
