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
manages the startup and execution of user jobs. :program:`flux shell` runs as
the job user, reads the jobspec and assigned resource set R for the job from
the KVS, and using this data determines what local job tasks to execute. While
job tasks are running, the job shell acts as the interface between the
Flux instance and the job by handling standard I/O, signals, and finally
collecting the exit status of tasks as they complete.

The job shell is designed to be customizable through both configuration and
extension mechanisms:

**Configuration**: Runtime behavior can be controlled via shell options
specified in jobspec. These options control features like CPU affinity,
output redirection, signal handling, and more. See :man7:`flux-shell-options`
for a complete list of available options.

**Extension**: The shell supports both builtin and runtime-loadable plugins
that implement most advanced features. Plugins can be written in C/C++ for
maximum performance, or in Lua for rapid development. Builtin plugins provide
core functionality including I/O redirection, CPU/GPU affinity, PMI support,
debugger integration (MPIR, ptrace), and more. See :man7:`flux-shell-plugins`
for details on the plugin architecture and development guide.

**Initialization**: Shell behavior can be customized via a Lua-based
``initrc`` file which is executed at startup. The initrc can load plugins,
set options, modify the environment, and even extend the shell with inline
Lua code. See :man5:`flux-shell-initrc` for the initrc format and API.

The shell runs with restricted Flux service access as a guest user,
limiting its capabilities to job-specific operations. This security
boundary ensures job shells cannot interfere with the Flux instance or
other jobs.

OPTIONS
=======

.. option:: -h, --help

   Summarize available options.

.. option:: --reconnect

   Attempt to reconnect if broker connection is lost. This option allows
   the shell to survive broker restarts during job execution.

OPERATION
=========

When a job has been granted resources by a Flux instance, a :program:`flux
shell` process is invoked on each broker rank involved in the job. The job
shell runs as the job user, and will always have :envvar:`FLUX_KVS_NAMESPACE`
set such that the root of the job shell's KVS accesses will be the guest
namespace for the job.

Shell Lifecycle
---------------

Each :program:`flux shell` connects to the local broker, fetches the jobspec
and resource set **R** for the job from the job-info module, and uses this
information to plan which tasks to locally execute.

Each shell proceeds through the following lifecycle phases:

**Initialization Phase**

 * Connect to local Flux broker
 * Call ``shell.connect`` plugin callbacks (builtin plugins only, as external
   plugins have not been loaded yet)
 * Register job-specific service endpoint: ``<userid>-shell-<jobid>``
 * Load system default ``initrc.lua`` from ``$sysconfdir/flux/shell/initrc.lua``
   (See :man7:`flux-shell-options` for override options)
 * Load user initrc if specified via ``userrc`` option
 * Compute task mapping from jobspec and R (call ``taskmap.SCHEME`` if
   non-default taskmap selected by user)
 * Call ``shell.init`` plugin callbacks
 * Change working directory to job's cwd
 * Enter initialization barrier (wait for all shells)
 * Emit ``shell.init`` event to exec.eventlog
 * Call ``shell.post-init`` plugin callbacks

**Task Launch Phase**

For each local task, the shell will:

 * Call ``task.init`` plugin callback
 * Fork task process
 * Call ``task.exec`` plugin callback (in child process, pre-exec)
 * Execute task via :linux:man2:`execve`
 * Call ``task.fork`` plugin callback (in parent process, post-fork)

**Running Phase**

 * Call ``shell.start`` plugin callbacks once all local tasks are started
 * Enter "start" barrier (wait for all shells)
 * Emit ``shell.start`` event to exec.eventlog
 * Monitor running tasks

   - Handle I/O redirection (see :ref:`io_handling`)
   - Forward signals to tasks (see :ref:`signal_handling`)
   - Respond to job exceptions

 * For each exiting task:

   - Collect task wait status
   - Call ``task.exit`` plugin callback

**Completion Phase**

 * Call ``shell.finish`` plugin callback when all local tasks have exited
   (reactor still active - suitable for asynchronous cleanup)
 * Exit reactor
 * Call ``shell.exit`` plugin callback after reactor has stopped
   (final synchronous cleanup only)
 * Exit with maximum task exit code

Barriers and Synchronization
-----------------------------

The shell uses distributed barriers at key points to ensure coordinated
execution across all shells in a multi-node job:

**Initialization barrier**: Ensures all shells have completed initialization
before any tasks start. This allows ``shell.post-init`` callbacks to rely
on all shells being fully initialized. If a shell fails before this barrier,
then the execution system will raise a start exception for the job. A timeout
is imposed on the start barrier by the job execution system. See
:option:`barrier-timeout` in :man5:`flux-config-exec`.

**Start barrier**: Ensures all shells have started their local tasks before
the ``shell.start`` event is emitted. This guarantees that when a plugin
or external tool sees the ``shell.start`` event, all tasks are known to be
running.

These barriers are critical for plugins that need to coordinate actions
across shells.

.. _io_handling:

I/O Handling
------------

The job shell manages standard I/O streams for all tasks via the shell
builtin input and output plugins.

**Standard input**: Input can be configured to be read from the KVS via the
``guest.input`` eventlog or directly from a file. Input may be broadcast to
all tasks (the default) or delivered to a subset of tasks. See the
:option:`flux-submit --input` option and ``input.*`` options in
:man7:`flux-shell-options` for configuration.

When input is read from the KVS, all job shells watch the input eventlog and
deliver data to matching tasks as it appears. For interactive jobs, the rank 0
(leader) shell registers a standard input service to which clients (typically
:man1:`flux job attach`) write input. The leader shell then writes this data
to the input eventlog for distribution to all other shells in the job. The
input eventlog acts as a permanent record of job input.

For file-based input, each task's stdin is connected directly to the input
file. An RFC 24 ``redirect`` event is posted to the input eventlog as a
record of the input file path.

**Standard output and error**: By default, output is directed to the output
eventlog in the KVS, encoded as UTF-8. Each shell rank captures output from
its local tasks. Non-leader ranks forward their captured output to a service
on the leader rank, which batches output eventlog entries for a configurable
period (see ``output.batch-timeout`` in :man7:`flux-shell-options`) before
posting them to the ``guest.output`` eventlog.

Output is sent to the leader using a credit-based flow control scheme to
prevent unbounded memory growth when tasks produce output faster than it can
be consumed.

By default, stdout is line-buffered and stderr is unbuffered. The buffering
mode can be changed through the ``output.{stdout,stderr}.buffer.type``
option, or the :option:`flux-submit --unbuffered` option sets both streams
to unbuffered and reduces the batch timeout.

Output may also be redirected to files. Output filenames support mustache
templates to generate unique names per job, node, or task. See the MUSTACHE
TEMPLATES section in :man1:`flux-submit` for more information. When using a
single file per job, output is still sent to the leader shell first, which
writes to the file. When using one or more files per shell, each shell writes
output directly, avoiding messages to the leader. A ``redirect`` event is
posted to the output eventlog when file output is used.

**PTY support**: The shell can allocate pseudo-terminals for tasks, enabling
terminal-aware programs to run correctly. See ``pty.*`` options in
:man7:`flux-shell-options`, or :option:`flux-submit --pty`.

.. _signal_handling:

Signal Handling
---------------

The job shell traps ``SIGINT``, ``SIGTERM``, and ``SIGALRM`` and forwards
them to all job tasks, allowing the job execution system to signal the shell
and have those signals propagated to tasks. The shell ignores ``SIGPIPE`` to
prevent premature termination from broken pipes. All other signals retain
their default disposition.

The job shell can also be configured to deliver a user-specified signal to
tasks a configurable period before a job's time limit. See the
:option:`flux-submit --signal` option or ``signal`` option in
:man7:`flux-shell-options` for details.

EXIT STATUS
===========

The shell exits with the highest exit code of any task. If any task was
killed by a signal, the shell exits with 128 + signal number.

RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man5:`flux-shell-initrc`, :man7:`flux-shell-plugins`,
:man7:`flux-shell-options`, :man1:`flux-run`, :man1:`flux-submit`,
:man1:`flux-job`, :man5:`flux-config-exec`
