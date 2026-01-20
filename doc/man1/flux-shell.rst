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
design can be found in the `PLUGINS`_ section below.

:program:`flux shell` also supports configuration via a Lua-based configuration
file, called the shell ``initrc``, from which shell plugins may be loaded
or shell options and data examined or set. The :program:`flux shell` initrc may
even extend the shell itself via simple shell plugins developed directly
in Lua. See the `SHELL INITRC`_ section below for details of the ``initrc``
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

 * register service endpoint specific to the job and userid,
   typically ``<userid>-shell-<jobid>``
 * load the system default ``initrc.lua``
   (``$sysconfdir/flux/shell/initrc.lua``), unless overridden by
   configuration (See `SHELL OPTIONS`_ and `SHELL INITRC`_ sections below)
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
file itself (see `SHELL INITRC`_ section, ``plugin.register()`` below)

By default, :program:`flux shell` supports the following plugin callback
topics:

**taskmap.SCHEME**
  Called when a taskmap scheme *SCHEME* is requested via the taskmap
  shell option or corresponding :option:`flux submit --taskmap` option.
  Plugins that want to offer a different taskmap scheme than the defaults
  of ``block``, ``cyclic``, ``hostfile``, and ``manual`` can register a
  ``taskmap.*`` plugin callback and then users can request this mapping
  with the appropriate :option:`flux submit --taskmap=name`` option.
  The default block taskmap is passed to the plugin as "taskmap" in the
  plugin input arguments, and the plugin should return the new taskmap as a
  string in the output args.  This callback is called before ``shell.init``.

**shell.connect**
  Called just after the shell connects to the local Flux broker. (Only
  available to builtin shell plugins.)

**shell.init**
  Called after the shell has finished fetching and parsing the
  **jobspec** and **R** from the KVS, but before any tasks
  are started.

**shell.post-init**
  Called after the shell initialization barrier has completed, but
  before starting any tasks.

**task.init**
  Called for each task after the task info has been constructed
  but before the task is executed.

**task.exec**
  Called for each task after the task has been forked just before
  :linux:man2:`execve` is called. This callback is made from within the
  task process.

**task.fork**
  Called for each task after the task if forked from the parent
  process (:program:`flux shell` process)

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

.. _flux_shell_options:

SHELL OPTIONS
=============

On startup, :program:`flux shell` will examine the jobspec for any shell
specific options under the ``attributes.system.shell.options`` key.  These
options may be set by the :option:`flux submit -o, --setopt=OPT` option,
or explicitly added to the jobspec by other means.

Job shell options may be switches to enable or disable a shell feature or
plugin, or they may take an argument. Because jobspec is a JSON document,
job shell options in jobspec may take arguments that are themselves
JSON objects. This allows maximum flexibility in runtime configuration
of optional job shell behavior. In the list below, if an option doesn't
include a ``=``, then it is a simple boolean option or switch and may be
specified simply with :option:`flux submit -o OPTION`.

Job shell plugins may also support configuration via shell options in
the jobspec. For specific information about runtime-loaded plugins,
see the documentation for the specific plugin in question.

Shell options supported by :program:`flux shell` itself and its built-in
plugins include:

.. option:: verbose[=INT]

  Set the shell verbosity to *INT*. A larger value indicates increased
  verbosity, though setting this value larger than 2 currently has no
  effect.

.. option:: nosetpgrp

  Disable the use of :linux:man2:`setpgrp` to launch each
  job task in its own process group. This will cause signals to be
  delivered only to direct children of the shell.

.. option:: initrc=FILE

  Load :program:`flux shell` initrc.lua file from *FILE* instead of the default
  initrc path. For details of the job shell initrc.lua file format,
  see the `SHELL INITRC`_ section below.

.. option:: userrc=FILE

  Load another initrc.lua file after the system one.  For details of the
  job shell initrc.lua file format, see the `SHELL INITRC`_ section below.

.. option:: pty

  Allocate a pty to all task ranks for non-interactive use. Output
  from all ranks will be captured to the same location as ``stdout``.
  This is the same as setting :option:`pty.ranks=all` and :option:`pty.capture`.
  (see below).

.. option:: pty.ranks=OPT

  Set the task ranks for which to allocate a pty. *OPT* may be either
  an RFC 22 IDset of target ranks, an integer rank, or the string "all"
  to indicate all ranks.

.. option:: pty.capture

  Enable capture of pty output to the same location as stdout. This is
  the default unless :option:`pty.interactive` is set.

.. option:: pty.interactive

  Enable a a pty on rank 0 that is set up for interactive attach by
  a front-end program (i.e. :program:`flux job attach`). With no other
  :option:`pty` options, only rank 0 will be assigned a pty and output will not
  be captured. These defaults can be changed by setting other
  :option:`pty` options after :option:`pty.interactive`, e.g.

  .. code-block:: console

    $  flux run -o pty.interactive -o pty.capture ...

  would allocate an interactive pty on rank 0 and also capture the
  pty session to the KVS (so it can be displayed after the job exits
  with ``flux job attach``).

.. option:: cpu-affinity=OPT

  Adjust the operation of the builtin shell ``affinity`` plugin.  If the
  option is unspecified, ``on`` is assumed.  *OPT* may be set to:

  on
    Bind each task to the full set of cores allocated to the job.

  off
    Disable the affinity plugin.  This may be useful if using another plugin
    such as `mpibind <https://github.com/LLNL/mpibind>`_ to manage CPU
    affinity.

  per-task
    Bind each task to an evenly populated subset of the cores allocated to
    the job.  Tasks share cores only if there are more tasks than cores.

  map:LIST
    Bind each task to *LIST*, a semicolon-delimited list of cores.
    Each entry in the list can be in one of the :linux:man7:`hwloc`
    *list*, *bitmask*, or *taskset* formats. See `hwlocality_bitmap(3)
    <https://www.open-mpi.org/projects/hwloc/doc/v2.9.0/a00181.php>`_,
    especially the :func:`hwloc_bitmap_list_snprintf`,
    :func:`hwloc_bitmap_snprintf` and :func:`hwloc_bitmap_taskset_snprintf`
    functions.

  verbose
    Log the cpuset assigned to the shell and each task (if :option:`per-task`
    is used). When combined with options listed above, the :option:`verbose`
    option must come first followed by a comma.

  dry-run
   Print cpusets but do not actually apply CPU affinity bindings. Useful
   for testing and debugging affinity configurations. Implies
   :option:`verbose`. When used with options listed above, the
   :option:`dry-run` option must come first followed by a comma.


.. option:: gpu-affinity=OPT

  Adjust operation of the builtin shell ``gpubind`` plugin.  This plugin
  sets :envvar:`CUDA_VISIBLE_DEVICES` to the GPU IDs allocated to the job.
  If the option is unspecified, ``on`` is assumed.  *OPT* may be set to:

  on
    Constrain each task to the full set of GPUs allocated to the job.

  off
    Disable the gpu-affinity plugin.

  per-task
    Constrain each task to an evenly populated subset of the GPUs allocated
    to the job.  Tasks share GPUs only if there are more tasks than GPUs.

  map:LIST
    Constrain each task to *LIST*, a semicolon-delimited list of GPUs.
    See :option:`cpu-affinity` above for a description of *LIST* format.

.. option:: stop-tasks-in-exec

  Stops tasks in ``exec()`` using ``PTRACE_TRACEME``. Used for debugging
  parallel jobs. Users should not need to set this option directly.

.. option:: oom.adjust=VALUE

  Adjust each task's OOM score to make it more or less likely to be
  killed when system memory is critically low.  A value of 1000 maximizes
  the task's probability of being selected, while a value of -1000 would
  prevent the task from being selected.  However, setting a negative value
  is normally a privileged operation.

  For more information, refer to :option:`oom_score_adj` in
  :linux:man5:`proc`.

.. option:: output.{stdout,stderr}.type=TYPE

  Set job output to for **stderr** or **stdout** to *TYPE*. *TYPE* may
  be one of ``term``, ``kvs`` or ``file`` (Default: ``kvs``). If only
  ``output.stdout.type`` is set, then this option applies to both
  ``stdout`` and ``stderr``. If set to ``file``, then ``output.<stream>.path``
  must also be set for the stream.

  See also: :option:`flux-submit --output`, :option:`flux-submit --error`.

.. option:: output.limit=SIZE

   Truncate KVS output after SIZE bytes have been written. SIZE may
   be a floating point value with optional SI units k, K, M, G.  The maximum
   value is 1G.  The default KVS output limit is 10M for jobs
   in a multi-user instance or 1G for single-user instance jobs.
   This value is ignored if output is directed to a file.

.. option:: output.{stdout,stderr}.path=PATH

  Set job stderr/out file output to PATH.

.. option:: output.mode=truncate|append

  Set the mode in which output files are opened to either truncate or
  append. The default is to truncate.

.. option:: input.stdin.type=TYPE

  Set job input for **stdin** to *TYPE*. *TYPE* may be either ``service``
  or ``file``.

  See also: :option:`flux-submit --input`.

.. option:: exit-timeout=VALUE

  When the first task in a job exits, and exit-timeout is not set to
  ``none``, a timer is initiated with a duration specified by *VALUE*. If the
  timer expires, a fatal exception is raised on the job. The timeout
  duration can be adjusted by specifying a new *VALUE* in Flux Standard
  Duration format. Setting the value to "none" disables both the timer
  and the exception. The default timeout is "30s" for normal parallel jobs
  (:man1:`flux-submit` and :man1:`flux-run`) and "none" for jobs that are
  instances of Flux (:man1:`flux-batch` and :man1:`flux-alloc`).

.. option:: exit-on-error

  If the first task to exit was signaled or exited with a nonzero status,
  raise a fatal exception on the job immediately.

.. option:: rlimit

  A dictionary of soft process resource limits to apply to the job before
  starting tasks. Resource limits are set to integer values by lowercase
  name without the ``RLIMIT_`` prefix, e.g. ``core`` or ``nofile``.

  See also: :option:`flux-submit --rlimit`.

.. option:: taskmap

  Request an alternate job task mapping. This option is an object
  consisting of required key ``scheme`` and optional key ``value``. The
  shell will attempt to call a ``taskmap.scheme`` plugin callback in the
  shell to invoke the alternate requested mapping. If ``value`` is set,
  this will also be passed to the invoked plugin.

  See also: :option:`flux-submit --taskmap`.

.. option:: pmi=LIST

  Specify a comma-separated list of PMI implementations to enable.  If the
  option is unspecified, ``simple`` is assumed.  To disable, set *LIST* to
  ``off``.  Available implementations include

  simple
    The simple PMI-1 wire protocol.  This implementation works by passing an
    open file descriptor to clients via the :envvar:`PMI_FD` environment
    variable.  It must be enabled when Flux's ``libpmi.so`` or ``libpmi2.so``
    libraries are used, and is preferred by :man1:`flux-broker`
    when Flux launches Flux, e.g. by means of :man1:`flux-batch` or
    :man1:`flux-alloc`.

  pmi1, pmi2
    Aliases for ``simple``.

  cray-pals
    Provided via external plugin from the
    `flux-coral2 <https://github.com/flux-framework/flux-coral2>`_ project.

  pmix
    Provided via external plugin from the
    `flux-pmix <https://github.com/flux-framework/flux-pmix>`_ project.

.. option:: pmi-simple.nomap

  Skip pre-populating the ``flux.taskmap`` and ``PMI_process_mapping`` keys
  in the ``simple`` implementation.

.. option:: pmi-simple.exchange.k=N

  Configure the PMI plugin's built-in key exchange algorithm to use a
  virtual tree fanout of ``N`` for key gather/broadcast in the ``simple``
  implementation.  The default is 2.

.. option:: stage-in

  Copy files to the directory referenced by :envvar:`FLUX_JOB_TMPDIR` that
  were previously archived with :man1:`flux-archive`.

.. option:: stage-in.names=LIST

  Select archives to extract by specifying a comma-separated list of names
  If no names are specified, ``main`` is assumed.

.. option:: stage-in.pattern=PATTERN

  Further filter the selected files to copy using a :man7:`glob` pattern.

.. option:: stage-in.destination=[SCOPE:]PATH

  Copy files to the specified destination instead of the directory referenced
  by :envvar:`FLUX_JOB_TMPDIR`.  The argument is a directory with optional
  *scope* prefix.  A scope of ``local`` denotes a local file system (the
  default), and a scope of ``global`` denotes a global file system.  The copy
  takes place on all the job's nodes if the scope is local, versus only the
  first node of the job if the scope is global.

.. warning::
  The directory referenced by :envvar:`FLUX_JOB_TMPDIR` is cleaned up when the
  job ends, is guaranteed to be unique, and is generally on fast local storage
  such as a *tmpfs*.  If a destination is explicitly specified, use the
  ``global:`` prefix where appropriate to avoid overwhelming a shared file
  system, and be sure to clean up.

.. option:: signal=OPTION

  Deliver signal ``SIGUSR1`` to the job 60s before job expiration.
  To customize the signal number or amount of time before expiration to
  deliver the signal, the ``signal`` option may be an object with one
  or both of the keys ``signum`` or ``timeleft``. (See below)

  See also: :option:`flux-submit --signal`.

.. option:: signal.signum=NUMBER

  Send signal *NUMBER* to the job :option:`signal.timeleft` seconds before
  the time limit.

.. option:: signal.timeleft=TIME

  Send signal :option:`signal.signum` *TIME* seconds before job expiration.

.. option:: hwloc.xmlfile

  Write the job shell's copy of hwloc XML to a file and set ``HWLOC_XMLFILE``.
  Note that this option will also unset ``HWLOC_COMPONENTS`` since presence
  of this environment variable may cause hwloc to ignore ``HWLOC_XMLFILE``.

.. option:: hwloc.restrict

  With :option:`hwloc.xmlfile`, restrict the exported topology XML to only
  the resources assigned to the current job. By default the XML is not
  restricted.

.. option:: sysmon

  Log peak memory usage and the cpu load average on each shell at the
  end of the job.  Periodic tracing of memory and cpu load while
  the job is running may be viewed by increasing shell verbosity to
  :option:`verbose=2`.

.. option:: sysmon.period=FSD

  Sysmon tracing is driven by the Flux heartbeat event (normally every
  two seconds).  The polling period may be changed to a different value
  in RFC 23 Flux Standard Duration format.

.. option:: rexec-shutdown-timeout=FSD

  When all tasks exit, processes launched via the rexec server receive
  SIGTERM, then SIGKILL after this timeout (RFC 23 Flux Standard Duration).
  Default: 60s.


.. _flux_shell_initrc:

SHELL INITRC
============

At initialization, :program:`flux shell` reads a Lua initrc file which can be
used to customize the shell operation. The initrc is loaded by default from
``$sysconfdir/flux/shell/initrc.lua`` (or ``/etc/flux/shell/initrc.lua``
for a "standard" install), but a different path may be specified when
launching a job via the ``initrc`` shell option.  Alternatively, the ``userrc``
shell option can specify an initrc file to load after the system one.

A job shell initrc file may be used to adjust the shell plugin searchpath,
load specific plugins, read and set shell options, and even extend the
shell itself using Lua.

Since the job shell ``initrc`` is a Lua file, any Lua syntax is
supported. Job shell specific functions and tables are described below:

**plugin.searchpath**
  The current plugin searchpath. This value can be queried, set,
  or appended. E.g. to add a new path to the plugin search path:
  ``plugin.searchpath = plugin.searchpath .. ':' .. path``

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
  Current :program:`flux shell` verbosity. This value may be changed at
  runtime, e.g. ``shell.options.verbose = 2`` to set maximum verbosity.

**shell.info**
  Returns a Lua table of shell information obtained via
  :man3:`flux_shell_get_info`. This table includes

  **jobid**
    The current jobid.
  **rank**
    The rank of the current shell within the job.
  **size**
    The number of :program:`flux shell` processes participating in this job.
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

  **id**
    The current shell rank (matches ``shell_rank`` input parameter)
  **name**
    The host name for ``shell_rank``
  **broker_rank**
    The broker rank on which ``shell_rank`` is running.
  **ntasks**
    The number of local tasks assigned to ``shell_rank``.
  **resources**
    A table of resources by name (e.g. "core", "gpu") assigned to
    ``shell_rank``, e.g. ``{ ncores = 2, core = "0-1", gpu = "0" }``.

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

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-submit`
