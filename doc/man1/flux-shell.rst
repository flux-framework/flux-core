==============
flux-shell(1)
==============


SYNOPSIS
========

**flux-shell** [*OPTIONS*] *JOBID*

DESCRIPTION
===========

.. program:: flux shell

:program:`flux shell`, the Flux job shell, is the component of Flux which
manages the startup and execution of user jobs.  :program:`flux shell` runs as
the job user, reads the jobspec and assigned resource set R for the job from
the KVS, and using this data determines what local job tasks to execute. While
job tasks are running, the job shell acts as the interface between the
Flux instance and the job by handling standard I/O, signals, and finally
collecting the exit status of tasks as they complete.

The design of the Flux job shell allows customization through a set of
builtin and runtime loadable shell plugins. These plugins are used to
handle standard I/O redirection, PMI, CPU and GPU affinity, debugger
support and more. Details of the :program:`flux shell` plugin capabilities and
design can be found in :man7:`flux-shell-plugins`.

:program:`flux shell` also supports configuration via a Lua-based configuration
file, called the shell ``initrc``, from which shell plugins may be loaded
or shell options and data examined or set. The :program:`flux shell` initrc may
even extend the shell itself via simple shell plugins developed directly
in Lua. See :man5:`flux-shell-initrc` for details of the ``initrc``
format and features.

OPTIONS
=======

.. option:: -h, --help

   Summarize available options.

.. option:: --reconnect

   Attempt to reconnect if broker connection is lost.

OPERATION
=========

When a job has been granted resources by a Flux instance, a
:program:`flux shell` process is invoked on each broker rank involved in the
job. The job shell runs as the job user, and will always have
:envvar:`FLUX_KVS_NAMESPACE` set such that the root of the job shell's
KVS accesses will be the guest namespace for the job.

Each :program:`flux shell` connects to the local broker, fetches the jobspec
and resource set **R** for the job from the job-info module, and uses this
information to plan which tasks to locally execute.

Once the job shell has successfully gathered job information, the
:program:`flux shell` then goes through the following general steps to manage
execution of the job:

 * connect to Flux and call ``shell.connect`` plugin callbacks
 * register service endpoint specific to the job and userid,
   typically ``<userid>-shell-<jobid>``
 * load the system default ``initrc.lua``
   (``$sysconfdir/flux/shell/initrc.lua``), unless overridden by
   configuration (See :man7:`flux-shell-options` and :man5:`flux-shell-initrc`)
 * call ``shell.init`` plugin callbacks
 * change working directory to the cwd of the job
 * enter a barrier to ensure shell initialization is complete on all shells
 * emit ``shell.init`` event to exec.eventlog
 * call ``shell.post-init`` plugin callbacks
 * create all local tasks. For each task, the following procedure is used

   - call ``task.init`` plugin callback
   - launch task, call ``task.exec`` plugin callback just before :linux:man2:`execve`
   - call ``task.fork`` plugin callback

 * once all tasks have started, call ``shell.start`` plugin callback
 * enter shell "start" barrier
 * emit ``shell.start`` event, after which all tasks are known running
 * for each exiting task:

   - call ``task.exit`` plugin callback
   - collect exit status

 * call ``shell.finish`` plugin callback when all tasks have exited
 * call ``shell.exit`` plugin callback when the shell has exited the
   reactor
 * exit with max task exit code

.. note::
   The ``shell.finish`` callback is called while the reactor is still
   active, making it suitable for cleanup operations that require an
   active event loop or time-sensitive execution. The ``shell.exit``
   callback is called after the reactor has exited and should only be
   used for final synchronous cleanup.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man5:`flux-shell-initrc`, :man7:`flux-shell-plugins`,
:man7:`flux-shell-options`
