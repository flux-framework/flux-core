==============
flux-shell(1)
==============


SYNOPSIS
========

**flux-shell** [*OPTIONS*] *JOBID*

DESCRIPTION
===========

flux-shell(1), the Flux job shell, is the component of Flux which manages
the startup and execution of user jobs. flux-shell(1) runs as the job user,
reads the jobspec and assigned resource set R for the job from the KVS,
and using this data determines what local job tasks to execute. While
job tasks are running, the job shell acts as the interface between the
Flux instance and the job by handling standard I/O, signals, and finally
collecting the exit status of tasks as they complete.

The design of the Flux job shell allows customization through a set of
builtin and runtime loadable shell plugins. These plugins are used to
handle standard I/O redirection, PMI, CPU and GPU affinity, debugger
support and more. Details of the flux-shell(1) plugin capabilities and
design can be found in the PLUGINS section below.

flux-shell(1) also supports configuration via a Lua-based configuration
file, called the shell ``initrc``, from which shell plugins may be loaded
or shell options and data examined or set. The flux-shell(1) initrc may
even extend the shell itself via simple shell plugins developed directly
in Lua. See the SHELL INITRC section below for details of the ``initrc``
format and features.

OPTIONS
=======

**-h, --help**
   Summarize available options.

**--reconnect**
   Attempt to reconnect if broker connection is lost.

OPERATION
=========

When a job has been granted resources by a Flux instance, a flux-shell(1)
process is invoked on each broker rank involved in the job. The job
shell runs as the job user, and will always have ``FLUX_KVS_NAMESPACE``
set such that the root of the job shell's KVS accesses will be the guest
namespace for the job.

Each flux-shell(1) connects to the local broker, fetches the jobspec and
resource set **R** for the job from the job-info module, and uses this
information to plan which tasks to locally execute.

Once the job shell has successfully gathered job information, the
flux-shell(1) then goes through the following general steps to manage
execution of the job:

 * register service endpoint specific to the job and userid,
   typically ``<userid>-shell-<jobid>``
 * load the system default ``initrc.lua``
   (``$sysconfdir/flux/shell/initrc.lua``), unless overridden by
   configuration (See JOBSPEC OPTIONS and INITRC sections below)
 * call ``shell.init`` plugin callbacks
 * change working directory to the cwd of the job
 * enter a barrier to ensure shell initialization is complete on all shells
 * emit ``shell.init`` event to exec.eventlog
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

 * call ``shell.exit`` plugin callback when all tasks have exited.
 * exit with max task exit code

PLUGINS
=======

The job shell supports external and builtin plugins which implement most
of the advanced job shell features. Job shell plugins are loaded into
a plugin stack by name, where the last loaded name wins. Therefore, to
override a builtin plugin, an alternate plugin which registers the same
name may be loaded at runtime.

.. note::
   Job shell plugins should be written with the assumption their access
   to Flux services may be restricted as a guest.

C plugins are defined using the Flux standard plugin format. A shell C
plugin should therefore export a single symbol ``flux_plugin_init()``, in
which calls to ``flux_plugin_add_handler(3)`` should be used to register
functions which will be invoked at defined points during shell execution.
These callbacks are defined by "topic strings" to which plugins can
"subscribe" by calling ``flux_plugin_add_handler(3)`` and/or
``flux_plugin_register(3)`` with topic :linux:man7:`glob` strings.

.. note::
   ``flux_plugin_init(3)`` is not called for builtin shell plugins. If
   a dynamically loaded plugin wishes to set shell options to influence
   a shell builtin plugin (e.g. to disable its operation), it should
   therefore do so in ``flux_plugin_init()`` in order to guarantee that
   the shell option is set before the builtin attempts to read them.

Simple plugins may also be developed directly in the shell ``initrc.lua``
file itself (see INITRC section, ``plugin.register()`` below)

By default, flux-shell supports the following plugin callback topics:

**taskmap.SCHEME**
  Called when a taskmap scheme *SCHEME* is requested via the taskmap
  shell option or corresponding ``--taskmap`` option of :man1:`flux-submit`
  and related commands.  Plugins that want to offer a different taskmap
  scheme than the defaults of ``block``, ``cyclic``, and ``manual`` can
  register a ``taskmap.*`` plugin callback and then users can request this
  mapping with the appropriate ``--taskmap=name`` option. The default block
  taskmap is passed to the plugin as "taskmap" in the plugin input arguments,
  and the plugin should return the new taskmap as a string in the output args.
  This callback is called before ``shell.init``.

**shell.connect**
  Called just after the shell connects to the local Flux broker. (Only
  available to builtin shell plugins.)

**shell.init**
  Called after the shell has finished fetching and parsing the
  **jobspec** and **R** from the KVS, but before any tasks
  are started.

**task.init**
  Called for each task after the task info has been constructed
  but before the task is executed.

**task.exec**
  Called for each task after the task has been forked just before
  :linux:man2:`execve` is called. This callback is made from within the
  task process.

**task.fork**
  Called for each task after the task if forked from the parent
  process (flux-shell process)

**task.exit**
  Called for each task after it exits and wait_status is available.

**shell.start**
  Called after all local tasks have been started. The shell "start"
  barrier is called just after this callback returns.

**shell.log**
  Called by the shell logging facility when a shell component
  posts a log message.

**shell.log-setlevel**
  Called by the shell logging facility when a request to set the
  shell loglevel is made.


Note however, that plugins may also call into the plugin stack to create
new callbacks at runtime, so more topics than those listed above may be
available in a given shell instance.


JOBSPEC OPTIONS
===============

On startup, ``flux-shell`` will examine the jobspec for any shell specific
options under the ``attributes.system.shell.options`` key.  These options
may be set by the :man1:`flux-submit` and related commands ``-o, --setopt=OPT``
option, or explicitly added to the jobspec by other means.

Job shell options may be switches to enable or disable a shell feature or
plugin, or they may take an argument. Because jobspec is a JSON document,
job shell options in jobspec may take arguments that are themselves
JSON objects. This allows maximum flexibility in runtime configuration
of optional job shell behavior. In the list below, if an option doesn't
include a ``=``, then it is a simple boolean option or switch and may be
specified simply with ``-o option`` in commands like :man1:`flux run`.

Options supported by ``flux-shell`` proper include:

**verbose**\ =\ *INT*
  Set the shell verbosity to *INT*. A larger value indicates increased
  verbosity, though setting this value larger than 2 currently has no
  effect.

**nosetpgrp**\ =\ *INT*
  If nonzero, disables the use of :linux:man2:`setpgrp` to launch each
  job task in its own process group. This will cause signals to be
  delivered only to direct children of the shell.

**initrc**\ =\ *FILE*
  Load flux-shell initrc.lua file from *FILE* instead of the default
  initrc path. For details of the job shell initrc.lua file format,
  see the INITRC section below.

Job shell plugins may also support configuration via shell options in
the jobspec. For specific information about runtime-loaded plugins,
see the documentation for the specific plugin in question. The following
options are supported by the builtin plugins of ``flux-shell``:

**pty**
  Allocate a pty to all task ranks for non-interactive use. Output
  from all ranks will be captured to the same location as ``stdout``.
  This is the same as setting **pty.ranks=all** and **pty.capture**.
  (see below).

**pty.ranks**\ =\ *OPT*
  Set the task ranks for which to allocate a pty. *OPT* may be either
  an RFC 22 IDset of target ranks, an integer rank, or the string "all"
  to indicate all ranks.

**pty.capture**
  Enable capture of pty output to the same location as stdout. This is
  the default unless **pty.interactive** is set.

**pty.interactive**
  Enable a a pty on rank 0 that is set up for interactive attach by
  a front-end program (i.e. ``flux job attach``). With no other **pty**
  options, only rank 0 will be assigned a pty and output will not
  be captured. These defaults can be changed by setting other
  **pty** options after **pty.interactive**, e.g.

  .. code-block:: console

    $  flux run -o pty.interactive -o pty.capture ...

  would allocate an interactive pty on rank 0 and also capture the
  pty session to the KVS (so it can be displayed after the job exits
  with ``flux job attach``).

**cpu-affinity**\ =\ *OPT*
  Adjust the operation of the builtin shell ``affinity`` plugin.
  *OPT* may be set to ``off`` to disable the affinity plugin, or
  ``per-task`` to have available CPUs distributed to tasks.
  If *OPT* starts with ``map:``, then the rest of the option is taken
  as a semicolon-delimited list of cpus to allocate to each task. Each
  entry in the list can be in one of the :linux:man7:`hwloc` list,
  bitmask, or taskset formats (See
  `hwlocality_bitmap(3) <https://www.open-mpi.org/projects/hwloc/doc/v2.9.0/a00181.php>`_,
  especially the ``hwloc_bitmap_list_snprintf()``, ``hwloc_bitmap_snprintf()``
  and ``hwloc_bitmap_taskset_snprintf()`` functions).  The default is ``on``,
  which binds all tasks to the assigned set of cores in the job.

**gpu-affinity**\ =\ *OPT*
  Adjust operation of the builtin shell ``gpubind`` plugin, which simply
  sets ``CUDA_VISIBLE_DEVICES`` to the GPU IDs allocated to the job.
  *OPT* may be set to ``off`` to disable the plugin, or ``per-task``
  to divide allocated GPUs among tasks launched by the shell (sets a
  different GPU ID or IDs for each launched task). If *OPT* starts with
  ``map:``, then the rest of the option is a semicolon-delimited list
  of GPUs to assign to each task. See **cpu-affinity** documentation
  for a description of the ``map:`` list format.

**stop-tasks-in-exec**
  Stops tasks in ``exec()`` using ``PTRACE_TRACEME``. Used for debugging
  parallel jobs. Users should not need to set this option directly.

**output.{stdout,stderr}.type**\ =\ *TYPE*
  Set job output to for **stderr** or **stdout** to *TYPE*. *TYPE* may
  be one of ``term``, ``kvs`` or ``file`` (Default: ``kvs``). If only
  ``output.stdout.type`` is set, then this option applies to both
  ``stdout`` and ``stderr``. If set to ``file``, then ``output.<stream>.path``
  must also be set for the stream. Most users will not need to set
  this option directly, as it will be set automatically by options
  of higher level commands such as :man1:`flux-submit`.

**output.{stdout,stderr}.path**\ =\ *PATH*
  Set job stderr/out file output to PATH.

**input.stdin.type**\ =\ *TYPE*
  Set job input for **stdin** to *TYPE*. *TYPE* may be either ``service``
  or ``file``. Users should not need to set this option directly as it
  will be handled by options of higher level commands like :man1:`flux-submit`.

**exit-timeout**\ =\ *VALUE*
  A fatal exception is raised on the job 30s after the first task exits.
  The timeout period may be altered by providing a different value in
  Flux Standard Duration form.  A value of ``none`` disables generation of
  the exception.

**exit-on-error**
  If the first task to exit was signaled or exited with a nonzero status,
  raise a fatal exception on the job immediately.

**rlimit**
  A dictionary of soft process resource limits to apply to the job before
  starting tasks. Resource limits are set to integer values by lowercase
  name without the ``RLIMIT_`` prefix, e.g. ``core`` or ``nofile``. Users
  should not need to set this shell option as it is handled by commands
  like :man1:`flux-submit`.

**taskmap**
  Request an alternate job task mapping. This option is an object
  consisting of required key ``scheme`` and optional key ``value``. The
  shell will attempt to call a ``taskmap.scheme`` plugin callback in the
  shell to invoke the alternate requested mapping. If ``value`` is set,
  this will also be passed to the invoked plugin. Normally, this option will
  be set by the :man1:`flux-submit` and related commands --taskmap`` option.

**pmi=off**
  Disable the process management interface for parallel jobs (see below).

**pmi=LIST**
  Specify a comma-separated list of PMI implementations to enable.  If the
  option is unspecified, the ``simple`` PMI-1 wire protocol implementation
  is enabled.  Other options such as ``cray-pals`` or ``pmix`` may be
  installed on your system.

**pmi-simple.nomap**
  Skip populating the PMI ``flux.taskmap`` and ``PMI_process_mapping`` keys.

**pmi-simple.kvs=native**
  Use the native Flux KVS instead of the PMI plugin's built-in key exchange
  algorithm.

**pmi-simple.exchange.k=N**
  Configure the PMI plugin's built-in key exchange algorithm to use a
  virtual tree fanout of ``N`` for key gather/broadcast.  The default is 2.

**stage-in**
  Copy files to $FLUX_JOB_TMPDIR that were previously mapped using
  :man1:`flux-filemap`.

**stage-in.tags**\ =\ *LIST*
  Select files to copy by specifying a comma-separated list of tags.
  If no tags are specified, the ``main`` tag is assumed.

**stage-in.pattern**\ =\ *PATTERN*
  Further filter the selected files to copy using a :man7:`glob` pattern.

**stage-in.destination**\ =\ *[SCOPE:]PATH*
  Copy files to the specified destination instead of $FLUX_JOB_TMPDIR.
  The argument is a directory with optional *scope* prefix.  A scope of
  ``local`` denotes a local file system (the default), and a scope of
  ``global`` denotes a global file system.  The copy takes place on all the
  job's nodes if the scope is local, versus only the first node of the
  job if the scope is global.

**signal=OPTION**
  Deliver signal ``SIGUSR1`` to the job 60s before job expiration.
  To customize the signal number or amount of time before expiration to
  deliver the signal, the ``signal`` option may be an object with one
  or both of the keys ``signum`` or ``timeleft``. (See below)

**signal.signum**\ =\ *NUMBER*
  Send signal *NUMBER* to the job ``signal.timeleft`` seconds before
  the time limit.

**signal.timeleft**\ =\ *TIME*
  Send signal ``signal.signum`` *TIME* seconds before job expiration.

.. warning::
  The $FLUX_JOB_TMPDIR is cleaned up when the job ends, is guaranteed to
  be unique, and is generally on fast local storage such as a *tmpfs*.
  If a destination is explicitly specified, use the ``global:`` prefix
  where appropriate to avoid overwhelming a shared file system, and be sure
  to clean up.

SHELL INITRC
============

At initialization, flux-shell(1) reads a Lua initrc file which can be used
to customize the shell operation. The initrc is loaded by default from
``$sysconfdir/flux/shell/initrc.lua`` (or ``/etc/flux/shell/initrc.lua``
for a "standard" install), but a different path may be specified when
launching a job via the ``initrc`` shell option.

A job shell initrc file may be used to adjust the shell plugin searchpath,
load specific plugins, read and set shell options, and even extend the
shell itself using Lua.

Since the job shell ``initrc`` is a Lua file, any Lua syntax is
supported. Job shell specific functions and tables are described below:

**plugin.searchpath**
  The current plugin searchpath. This value can be queried, set,
  or appended. E.g. to add a new path to the plugin search path:
  ``plugin.searchpath = plugin.searchpath + ':' + path``

**plugin.load({file=glob, [conf=table]})**
  Explicitly load one more shell plugins. This function takes a table
  argument with ``file`` and ``conf`` arguments. The ``file`` argument
  is a glob of one or more plugins to load. If an absolute path is not
  specified, then the glob will be relative to ``plugin.searchpath``.
  E.g. ``plugin.load { file = "*.so" }`` will load all ``.so`` plugins in
  the current search path. The ``conf`` option allows static configuration
  values to be passed to plugin initialization functions when supported.

  For example a plugin ``test.so`` may be explicitly loaded with
  configuration via:

  .. code-block:: lua

    plugin.load { file = "test.so", conf = { value = "foo" } }

**plugin.register({name=plugin_name, handlers=handlers_table)**
  Register a Lua plugin. Requires a table argument with the plugin ``name``
  and a set of ``handlers``. ``handlers_table`` is an array of tables, each
  of which must define ``topic``, a topic glob of shell plugin callbacks to
  which to subscribe, and ``fn`` a handler function to call for each match

  For example, the following plugin would log the topic string for
  every possible plugin callback (except for callbacks which are made
  before the shell logging facility is initialized)

  .. code-block:: lua

    plugin.register {
      name = "test",
      handlers = {
         { topic = "*",
           fn = function (topic) shell.log ("topic="..topic) end
         },
      }
    }

**source(glob)**
  Source another Lua file or files. Supports specification of a glob,
  e.g. ``source ("*.lua")``.  This function fails if a non-glob argument
  specifies a file that does not exist, or there is an error loading or
  compiling the Lua chunk.

**source_if_exists(glob)**
  Same as ``source()``, but do not throw an error if the target file does
  not exist.

**shell.rcpath**
  The directory in which the current initrc file resides.

**shell.getenv([name])**
  Return the job environment (not the job shell environment). This is
  the environment which will be inherited by the job tasks. If called
  with no arguments, then the entire environment is copied to a table
  and returned. Otherwise, acts as :man3:`flux_shell_getenv` and returns
  the value for the environment variable name, or ``nil`` if not set.

**shell.setenv(var, val, [overwrite])**
  Set environment variable ``var`` to value ``val`` in the job environment.
  If ``overwrite`` is set and is ``0`` or ``false`` then do not overwrite
  existing environment variable value.

**shell.unsetenv(var)**
  Unset environment variable ``var`` in job environment.

**shell.options**
  A virtual index into currently set shell options, including those
  set in jobspec. This table can be used to check jobspec options,
  and even to force certain options to a value by default e.g.
  ``shell.options['cpu-affinity'] = "per-task"``, would force
  ``cpu-affinity`` shell option to ``per-task``.

**shell.options.verbose**
  Current flux-shell verbosity. This value may be changed at runtime,
  e.g. ``shell.options.verbose = 2`` to set maximum verbosity.

**shell.info**
  Returns a Lua table of shell information obtained via
  :man3:`flux_shell_get_info`. This table includes

  **jobid**
    The current jobid.
  **rank**
    The rank of the current shell within the job.
  **size**
    The number of flux-shell processes participating in this job.
  **ntasks**
    The total number of tasks in this job.
  **service**
    The service string advertised by the shell.
  **options.verbose**
    True if the shell is running in verbose mode.
  **jobspec**
    The jobspec of the current job
  **R**
    The resource set **R** of the current job

**shell.rankinfo**
  Returns a Lua table of rank-specific shell information for the
  current shell rank. See `shell.get_rankinfo()` for a description
  of the members of this table.

**shell.get_rankinfo(shell_rank)**
  Query rank-specific shell info as in the function call
  :man3:`flux_shell_get_rank_info`.  If ``shell_rank`` is not provided
  then the current rank is used.  Returns a table of rank-specific
  information including:

  **broker_rank**
    The broker rank on which ``shell_rank`` is running.
  **ntasks**
    The number of local tasks assigned to ``shell_rank``.
  **resources**
    A table of resources by name (e.g. "core", "gpu") assigned to
    ``shell_rank``, e.g. ``{ core = "0-1", gpu = "0" }``.

**shell.log(msg)**, **shell.debug(msg)**, **shell.log_error(msg)**
  Log messages to the shell log facility at INFO, DEBUG, and ERROR
  levels respectively.

**shell.die(msg)**
  Log a FATAL message to the shell log facility. This generates a
  job exception and will terminate the job.

The following task-specific initrc data and functions are available
only in one of the ``task.*`` plugin callbacks. An error will be
generated if they are accessed from any other context.

**task.info**
  Returns a Lua table of task specific information for the "current"
  task (see :man3:`flux_shell_task_get_info`). Included members of
  the ``task.info`` table include:

  **localid**
    The local task rank (i.e. within this shell)
  **rank**
    The global task rank (i.e. within this job)
  **state**
    The current task state name
  **pid**
    The process id of the current task (if task has been started)
  **wait_status**
    (Only in ``task.exit``) The status returned by
    :linux:man2:`waitpid` for this task.
  **exitcode**
    (Only in ``task.exit``) The exit code if ``WIFEXTED()`` is true.
  **signaled**
    (Only in ``task.exit``) If task was signaled, this member will be
    non-zero integer signal number that caused the task to exit.

**task.getenv(var)**
  Get the value of environment variable ``var`` if set in the current
  task's environment. This function reads the environment from the
  underlying ``flux_cmd_t`` for a shell task, and thus only makes sense
  before a task is executed, e.g. in ``task.init`` and ``task.exec``
  callbacks.

**task.unsetenv(var)**
  Unset environment variable ``var`` for the current task. As with
  ``task.getenv()`` this function is only valid before a task has
  been started.

**task.setenv(var, value, [overwrite])**
  Set environment variable ``var`` to ``val`` for the current task.
  If ``overwrite`` is set to ``0`` or ``false``, then do not overwrite
  any current value. As with ``task.getenv()`` and ``task.unsetenv()``,
  this function only has an effect before the task is started.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man1:`flux-submit`
