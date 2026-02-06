====================
flux-shell-initrc(5)
====================


DESCRIPTION
===========

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

**plugin.register({name=plugin_name, handlers=handlers)**
  Register a Lua plugin. Requires a table argument with the plugin ``name``
  and a set of ``handlers``. The ``handlers`` parameter is an array of
  tables, each of which must define ``topic``, a topic glob of shell plugin
  callbacks to which to subscribe, and ``fn`` a handler function to call
  for each match

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

:man1:`flux-shell`, :man7:`flux-shell-plugins`, :man7:`flux-shell-options`
