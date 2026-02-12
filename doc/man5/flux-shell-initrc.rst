====================
flux-shell-initrc(5)
====================

DESCRIPTION
===========

At initialization, :program:`flux shell` reads a Lua initrc file which
customizes shell operation. The initrc is loaded by default from
``$sysconfdir/flux/shell/initrc.lua`` (typically
``/etc/flux/shell/initrc.lua`` for a standard install), but a different
path may be specified via the ``initrc`` shell option. Additionally, the
``userrc`` shell option specifies an initrc file to load after the system one.

The initrc file is executed as a Lua script in an environment with access
to special shell-specific functions and tables. The initrc can:

- Adjust the shell plugin search path
- Load specific plugins with configuration
- Read and set shell options
- Modify job environment variables
- Extend the shell with inline Lua plugins
- Source additional Lua files

Since the initrc is a Lua file, any valid Lua syntax is supported, enabling
sophisticated shell configuration with conditionals, loops, and logic.

EXECUTION CONTEXT
=================

The initrc executes early in the shell lifecycle, after the shell connects
to Flux but before tasks are started.

**Available**: Shell connection to broker, jobspec, resource set R, shell
rank and size information.

**Not yet available**: Task information (tasks haven't been created),
local process environment (not yet modified by plugins).

**Scope**: Changes made in the initrc affect all subsequent shell operations
and plugin loading. This makes the initrc ideal for configuration that must
be in place before plugins initialize.

The initrc can register simple Lua plugins to be invoked in later stages of
the shell lifecycle.  See :ref:`plugin_register` and
:man7:`flux-shell-plugins`.

SYSTEM VS USER INITRC
======================

The shell supports two levels of initrc files:

**System initrc** (``/etc/flux/shell/initrc.lua``):

- Loaded automatically by the shell
- Defines default plugin loading behavior
- Sets system-wide defaults
- Can be completely replaced via ``initrc`` option (not recommended)

**User initrc** (specified via ``userrc`` option):

- Loaded by default system initrc via

  .. code-block:: lua

    -- If userrc is set in shell options, then load the user supplied
    -- initrc here:
    if shell.options.userrc then source (shell.options.userrc) end

- Extends or overrides system defaults
- Can load additional plugins
- Can modify environment or options
- Recommended for user customization

Example usage:

.. code-block:: console

  $ flux run -o userrc=$HOME/.flux/shell-initrc.lua myapp

.. warning::
  Using ``initrc`` to replace the system initrc bypasses all default
  plugin loading. This can break shell functionality. Use ``userrc``
  to extend the system configuration instead.

PLUGIN MANAGEMENT
=================

plugin.searchpath
-----------------

The current plugin search path. This is a colon-separated list of
directories where the shell looks for plugins.

Query the current search path:

.. code-block:: lua

  shell.log ("Plugin path: " .. plugin.searchpath)

Append to the search path:

.. code-block:: lua

  plugin.searchpath = plugin.searchpath .. ":/opt/flux/plugins"

Replace the search path:

.. code-block:: lua

  plugin.searchpath = "/custom/path"

The default search path is set to the system shell plugin directory
(typically ``/usr/lib64/flux/shell/plugins``) but can be overridden via
the :option:`conf.shell_pluginpath` broker attribute.
See :man7:`flux-broker-attributes`.

plugin.load(spec)
-----------------

Explicitly load one or more shell plugins. Takes a table argument with
``file`` and optional ``conf`` fields.

Parameters:

- ``file`` (required): Glob pattern or path to plugin file(s)
- ``conf`` (optional): Configuration table passed to plugin initialization

As a convenience, if ``spec`` is a string, then the function treats this
as a ``file`` argument. For example, these are equivalent:

.. code-block:: lua

  plugin.load("affinity.so")
  plugin.load({file = "affinity.so"})

If ``file`` is not an absolute path, it's interpreted relative to
``plugin.searchpath``.

Examples:

Load all .so plugins from search path:

.. code-block:: lua

  plugin.load { file = "*.so" }

or more simply:

.. code-block:: lua

  plugin.load ("*.so")

Load specific plugin:

.. code-block:: lua

  plugin.load { file = "affinity.so" }

Load plugin from absolute path:

.. code-block:: lua

  plugin.load ("/opt/flux/plugins/custom.so")

Load plugin with configuration:

.. code-block:: lua

  plugin.load {
    file = "myplugin.so",
    conf = {
      enabled = true,
      level = 2,
      options = { "foo", "bar" }
    }
  }

The ``conf`` table is available to the plugin's initialization function
via the ``flux_plugin_get_conf()`` API. How configuration is used depends
on the specific plugin.


.. _plugin_register:

plugin.register(spec)
---------------------

Register a Lua plugin defined inline in the initrc. Takes a table argument
with ``name`` and ``handlers`` fields.

Parameters:

- ``name`` (required): Plugin name (must be unique)
- ``handlers`` (required): Array of handler tables, each with:
  - ``topic``: Topic glob string to match callbacks
  - ``fn``: Lua function to call for matching topics

The Lua function receives the matched topic string as its first argument
when using glob patterns.

See PLUGIN CALLBACKS in :man7:`flux-shell-plugins` for available callback
topics.

Examples (see :ref:`examples` below for more extensive examples):

Simple plugin:

.. code-block:: lua

  plugin.register {
    name = "hello",
    handlers = {
      {
        topic = "shell.init",
        fn = function ()
          shell.log ("Hello from Lua plugin!")
        end
      }
    }
  }

Multiple handlers:

.. code-block:: lua

  plugin.register {
    name = "monitor",
    handlers = {
      {
        topic = "shell.init",
        fn = function ()
          shell.log ("Job starting")
        end
      },
      {
        topic = "shell.finish",
        fn = function ()
          shell.log ("Job complete")
        end
      }
    }
  }

Wildcard topic matching:

.. code-block:: lua

  plugin.register {
    name = "logger",
    handlers = {
      {
        topic = "task.*",
        fn = function (topic)
          shell.log ("Task event: " .. topic)
        end
      }
    }
  }

See :man7:`flux-shell-plugins` for available callback topics and detailed
information about plugin development.

FILE MANAGEMENT
===============

source(glob)
------------

Source one or more Lua files. Supports glob patterns.

The function fails (raising a Lua error) if:

- A non-glob path specifies a file that doesn't exist
- There's an error loading or compiling the Lua code

Examples:

Source single file:

.. code-block:: lua

  source ("/etc/flux/shell/custom.lua")

Source multiple files with glob:

.. code-block:: lua

  source ("/etc/flux/shell/rc.d/*.lua")

.. note::
   To source files relative to the current initrc path, use
   :ref:`shell_source_rcpath` below.

source_if_exists(glob)
----------------------

Same as ``source()`` but doesn't fail if the target file doesn't exist.
Useful for optional configuration files.

Examples:

Load optional user config:

.. code-block:: lua

  source_if_exists (os.getenv ("HOME") .. "/.flux/shell-rc.lua")

Load optional site config:

.. code-block:: lua

  source_if_exists ("/etc/flux/site/shell-extras.lua")


.. shell_source_rcpath

shell.source_rcpath(pattern)
----------------------------

Source all files matching a pattern from :ref:`shell_rcpath`.

For example, the default shell initrc calls:

.. code-block:: lua

  -- Source all rc files under shell.rcpath/lua.d/*.lua:
  shell.source_rcpath ("*.lua")


shell.source_rcpath_option(opt)
-------------------------------

Source all files matching `value[@version]`.lua from rcpath for option
``opt``.

For example, the default shell initrc calls:

.. code-block:: lua

  -- If -o mpi=openmpi@5 was specified, this sources:
  -- /etc/flux/shell/mpi/openmpi@5.lua (if it exists)
  shell.source_rcpath_option ("mpi")

This allows version specific mpi plugins to be placed in
``$rcpath/mpi/name@version.lua`` and selected via ``-o mpi=name@version``.


SHELL INFORMATION
=================

.. _shell_rcpath

shell.rcpath
------------

The directory containing the current initrc file. Useful for sourcing
related files or locating resources relative to the current initrc.

shell.info
----------

A table containing shell metadata. Read-only. Available fields:

**jobid**
  Job ID as a string.

**instance_owner**
  The instance owner userid of the enclosing instance

**rank**
  Shell rank (0 to size-1).

**size**
  Total number of shells participating in the job.

**ntasks**
  Total number of tasks across all shells.

**service**
  Shell service name (format: ``<userid>-shell-<jobid>``).

**options**
  Shell options table (i.e. ``attributes.system.shell.options``)
  from jobspec.

**jobspec**
  Full jobspec as a Lua table.

**R**
  Resource set R as a Lua table.

Example:

.. code-block:: lua

  local info = shell.info
  if info.rank == 0 then
    shell.log (string.format("Job %d starting on %d nodes with %d tasks",
               info.jobid, info.size, info.ntasks)
  end

shell.rankinfo
--------------

Information specific to the current shell rank. Equivalent to calling
``shell.get_rankinfo(shell.info.rank)``. See ``shell.get_rankinfo()`` below.

shell.get_rankinfo(shell_rank)
-------------------------------

Query rank-specific shell information. Returns a Lua table with:

**id**
  Shell rank (matches ``shell_rank`` parameter).

**name**
  Hostname where this shell is running.

**broker_rank**
  Broker rank on which this shell is running.

**ntasks**
  Number of tasks assigned to this shell rank.

**taskids**
  Task id list for this rank in RFC 22 idset form.

**resources**
  Table of resources by name assigned to this shell rank. Keys are resource
  names (e.g., ``"cores"``, ``"gpus"``), values are the idset of assigned
  resources from **R**.

  Also included is the count of cores under the key ``"ncores"``.

  Example resource table:

  .. code-block:: lua

    {
      ncores = 4,
      cores = "0-3",
      gpus = "0"
    }

Example:

.. code-block:: lua

  for rank = 0, shell.info.size - 1 do
    local rinfo = shell.get_rankinfo (rank)
    shell.log (string.format ("Rank %d (%s): %d cores, %d tasks",
                              rank,
                              rinfo.name,
                              rinfo.resources.ncores or 0,
                              rinfo.ntasks))
  end

SHELL OPTIONS
=============

shell.options
-------------

A virtual table providing access to shell options. Shell options can be
read, set, or checked for existence.

Read option:

.. code-block:: lua

  local verbosity = shell.options.verbose or 0
  local custom = shell.options.myplugin.enabled

Set option:

.. code-block:: lua

  shell.options['cpu-affinity'] = "per-task"

Setting options in the initrc is useful for establishing site-wide
defaults. However, in order to allow users to override these defaults,
the system initrc should first check for existence of a user provided
option before setting the desired default. For example:

.. code-block:: lua

  -- Set cpu-affinity=per-task by default, unless user specified
  -- cpu-affinity explicitly in jobspec:
  if shell.options['cpu-affinity'] == nil then
     shell.options['cpu-affinity'] = 'per-task'
  end


shell.options.verbose
---------------------

Current shell verbosity level. This value is read-only because shell logging
is initialized before initrc execution.

.. code-block:: lua

  -- Check shell verbosity
  if shell.options.verbose > 0 then
    shell.log ("Verbose mode active")
  end

shell.getopt_with_version(name)
-------------------------------

Get a top level option from the shell.options table that includes an
optional `@version` specification, e.g. ``openmpi@5``.

Returns
- ``val`` the option value or ``nil`` if shell option not set.
- ``version`` the option version or ``nil`` if no version specified.

.. code-block:: lua

  local mpi, version = shell.getopt_with_version("mpi")
  -- mpi = "openmpi", version = "5" for -o mpi=openmpi@5


ENVIRONMENT MANAGEMENT
======================

shell.getenv([name])
--------------------

Get job environment variables. This is the environment that will be
inherited by job tasks (not the shell's own environment).

Get single variable:

.. code-block:: lua

  local path = shell.getenv ("PATH")
  if path then
    shell.log ("Job PATH: " .. path)
  end

Get entire environment:

.. code-block:: lua

  local env = shell.getenv ()
  for key, value in pairs (env) do
    shell.log (key .. "=" .. value)
  end

Returns ``nil`` if the variable is not set (when called with name), or
a table of all environment variables (when called without arguments).

shell.setenv(var, val, [overwrite])
------------------------------------

Set an environment variable in the job environment.

Parameters:

- ``var``: Variable name
- ``val``: Value to set
- ``overwrite``: Optional boolean (default: true). If false, don't overwrite
  existing values.

Examples:

Set unconditionally:

.. code-block:: lua

  shell.setenv ("MY_VAR", "value")

Set if not already set:

.. code-block:: lua

  shell.setenv ("PATH", "/default/path", false)

Append to existing:

.. code-block:: lua

  local path = shell.getenv ("PATH") or ""
  shell.setenv ("PATH", "/new/path:" .. path)

shell.unsetenv(var)
-------------------

Remove an environment variable from the job environment.

.. code-block:: lua

  shell.unsetenv ("UNWANTED_VAR")

shell.prepend_path(env_var, path)
---------------------------------

Prepend ``path`` to colon-separated path environment variable ``env_var``.

.. code-block:: lua

  shell.prepend_path ("PATH", "/home/user/.local/bin/")


shell.env_strip(pattern, ...)
-----------------------------

Strip all environment variables from job environment that match one or more
pattern arguments.

.. code-block:: lua

  shell.env_strip("^FOO_", "^BAR_")

LOGGING
=======

The shell provides logging functions that respect the current verbosity
level and integrate with the shell's logging system.

shell.log(msg)
--------------

Log informational message. Always visible (verbosity >= 0).

.. code-block:: lua

  shell.log ("Plugin initialized")

shell.debug(msg)
----------------

Log debug message. Only visible with verbosity >= 1.

.. code-block:: lua

  shell.debug ("Processing configuration")

shell.log_error(msg)
--------------------

Log error message. Always visible and marked as error level.

.. code-block:: lua

  shell.log_error ("Configuration file not found")

shell.die(msg)
--------------

Log fatal error and raise a job exception. This will terminate the job.

.. code-block:: lua

  if not required_option then
    shell.die ("Required option 'foo' not set")
  end

TASK INFORMATION (Task Callbacks Only)
=======================================

The following task-specific functions and data are only available in
task-related plugin callbacks (``task.init``, ``task.exec``, ``task.fork``,
``task.exit``). Attempting to access ``task.*`` information outside of
task callbacks will raise a Lua error.

task.info
---------

A table containing information about the current task. Available fields
depend on the callback:

**Always available**:

- ``localid``: Local task rank (within this shell, 0 to ntasks-1)
- ``rank``: Global task rank (within entire job, 0 to total_tasks-1)
- ``state``: Current task state name

**Available after fork** (``task.fork``, ``task.exit``):

- ``pid``: Process ID of the task

**Available in task.exit**:

- ``wait_status``: Raw status from :linux:man2:`waitpid`
- ``exitcode``: Exit code (if ``WIFEXITED`` is true). If task was signaled
  then exitcode will be set to 128 + signal number.
- ``signaled``: Signal number (if ``WIFSIGNALED`` is true)

Example in task.init:

.. code-block:: lua

  plugin.register {
    handlers = {
      {
        topic = "task.init",
        fn = function ()
          local info = task.info
          shell.log (string.format ("Initializing task %d (local %d)",
                                   info.rank, info.localid))
        end
      }
    }
  }

Example in task.exit:

.. code-block:: lua

  plugin.register {
    handlers = {
      {
        topic = "task.exit",
        fn = function ()
          local info = task.info
          if info.exitcode and info.exitcode ~= 0 then
            shell.log_error (string.format ("Task %d failed with code %d",
                                           info.rank, info.exitcode))
          elseif info.signaled then
            shell.log_error (string.format ("Task %d killed by signal %d",
                                           info.rank, info.signaled))
          end
        end
      }
    }
  }

task.getenv(var)
----------------

Get the value of an environment variable from the current task's
environment. Only meaningful before the task starts (``task.init``,
``task.exec``).

.. code-block:: lua

  local path = task.getenv ("PATH")

Returns ``nil`` if not set.

task.setenv(var, value, [overwrite])
-------------------------------------

Set an environment variable for the current task. Only meaningful before
the task starts.

Parameters same as ``shell.setenv()``.

.. code-block:: lua

  plugin.register {
    handlers = {
      {
        topic = "task.init",
        fn = function ()
          task.setenv ("TASK_RANK", tostring (task.info.rank))
          task.setenv ("LOCAL_RANK", tostring (task.info.localid))
        end
      }
    }
  }

task.unsetenv(var)
------------------

Remove an environment variable from the current task's environment. Only
meaningful before the task starts.

.. code-block:: lua

  task.unsetenv ("UNWANTED_VAR")

.. _examples

EXAMPLES
========

Example: minimal system initrc
------------------------------

A typical minimal system initrc that loads plugins:

.. code-block:: lua

  -- Load all .so plugins from plugin search path
  plugin.load { file = "*.so" }

  -- Source all Lua rc files
  shell.source_rcpath ("*.lua")

This is the essential content of the default system initrc. It loads all
compiled plugins and sources all Lua extensions.

Example: User Customization
----------------------------

A user initrc that extends the system configuration:

.. code-block:: lua

  -- Load custom plugin
  plugin.load { file = "/home/user/.flux/plugins/myplugin.so" }

  -- Adjust environment
  shell.prepend_path ("PATH", "/home/user/.local/bin/")

  -- Add custom variable
  shell.setenv ("MY_FLUX_JOB", shell.info.jobid)

Example: Conditional Plugin Loading
------------------------------------

Load plugins based on job characteristics:

.. code-block:: lua

  -- Only load profiler for large jobs
  if shell.info.ntasks >= 64 then
    plugin.load { file = "profiler.so", conf = { level = 2 } }
    shell.log ("Profiler enabled for large job")
  end

  -- Enable GPU plugin if GPUs are allocated
  local rinfo = shell.rankinfo
  if rinfo.resources.gpu then
    plugin.load { file = "gpu-monitor.so" }
  end

Example: Inline Lua Plugin
---------------------------

Define a simple monitoring plugin directly in the initrc:

.. code-block:: lua

  local task_count = 0

  plugin.register {
    name = "task-counter",
    handlers = {
      {
        topic = "task.init",
        fn = function ()
          task_count = task_count + 1
        end
      },
      {
        topic = "shell.start",
        fn = function ()
          shell.log (string.format (
            "Shell rank %d started %d tasks",
            shell.info.rank,
            task_count
          ))
        end
      }
    }
  }

Example: Environment Customization
-----------------------------------

Customize the task environment based on allocations:

.. code-block:: lua

  plugin.register {
    name = "env-setup",
    handlers = {
      {
        topic = "task.init",
        fn = function ()
          local info = task.info
          local rinfo = shell.get_rankinfo (shell.info.rank)

          -- Set resource-specific variables
          if rinfo.resources.cores then
            task.setenv ("ALLOCATED_CORES", rinfo.resources.cores)
          end

          if rinfo.resources.gpus then
            task.setenv ("ALLOCATED_GPUS", rinfo.resources.gpus)
          end
        end
      }
    }
  }

Example: Debug Logging
----------------------

Subscribe to all topics:

.. code-block:: lua

  plugin.register {
    name = "debug-logger",
    handlers = {
      {
        topic = "*",
        fn = function (topic)
          -- Do not log from a shell.log callback
          if topic == "shell.log" then return end
          shell.log ("Callback: " .. topic)
        end
      }
    }
  }

Example: Site-Wide Defaults
----------------------------

Establish site-wide defaults for a cluster:

.. code-block:: lua

  -- Site default: per-task CPU affinity
  if shell.options['cpu-affinity'] == nil then
    shell.options['cpu-affinity'] = "per-task"
  end

  -- Site default: extended exit timeout
  if shell.options['exit-timeout'] == nil then
    shell.options['exit-timeout'] = "5m"
  end

  -- Load site-specific monitoring
  plugin.load { file = "/opt/site/flux/monitor.so" }

  -- Set site-specific environment
  shell.setenv ("SITE_ID", "cluster-01", false)


Example: Adjust OOM Score
-------------------------

Increase OOM score for job tasks, but exclude the shell and broker processes.

.. code-block:: lua

  -- Function to write to the oom_score_adj file
  function write_oom_score_adj(value)
    local filename = "/proc/self/oom_score_adj"

    -- Open the file in write mode
    local file = io.open(filename, "w")

    if file then
      -- Write the value to the file
      file:write(value .. "\n")
      file:close()
    else
      shell.log("Error: Could not open " .. filename)
    end
  end

  -- Detect if command is a flux broker
  function is_flux_broker (shell)
    local basename = require 'posix'.basename
    local args = shell.info.jobspec.tasks[1].command
    if #args > 1 then
       local cmd = basename (args[1])
       local arg = args[2]
       if cmd == "flux" and (arg == "start" or arg == "broker") then
         return true
       end
    end
    return false
  end

  -- skip flux broker processes
  if is_flux_broker (shell) then
    return
  end

  -- otherwise, set oom_score_adj for each task
  plugin.register {
    name = "oom-adjust",
    handlers = {
       {
          topic = "task.exec",
          fn = function ()
             write_oom_score_adj (1000)
          end
       }
    }
  }


BEST PRACTICES
==============

Use userrc for Customization
-----------------------------

Instead of replacing the system initrc, use ``userrc`` for custom
configuration:

.. code-block:: console

  $ flux run -o userrc=$HOME/.flux/shell-rc.lua myapp

This preserves system defaults while adding custom behavior.

Check Before Setting
--------------------

When setting defaults, check if options are already set:

.. code-block:: lua

  if shell.options['cpu-affinity'] == nil then
    shell.options['cpu-affinity'] = "per-task"
  end

This allows jobspec options to override initrc defaults.

Handle Missing Values
---------------------

Always handle potentially missing environment variables or options:

.. code-block:: lua

  local path = shell.getenv ("PATH") or "/bin:/usr/bin"
  local verbosity = shell.options.verbose or 0

Use Appropriate Log Levels
---------------------------

Reserve ``shell.die()`` for truly fatal errors:

.. code-block:: lua

  -- Non-fatal: log and continue
  if not optional_config then
    shell.log_error ("Optional config not found, using defaults")
    -- continue...
  end

  -- Fatal: die
  if not required_config then
    shell.die ("Required config missing")
  end


TESTING
=======

Planned changes or enhancements to the default initrc can be tested
before installation using the ``initrc`` shell option:

.. code-block:: console

  $ flux run -o initrc=/path/to/initrc.lua ...

Individual Lua files can more easily be tested via the ``userrc`` option:

.. code-block:: console

  $ flux run -o userrc=test.lua ...

Testing can also be done in a test instance:

.. code-block:: console

  $ flux start -s 2 flux run -N2 -o userrc=test.lua ...

RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man1:`flux-shell`, :man7:`flux-shell-plugins`, :man7:`flux-shell-options`,
:man1:`flux-run`, :man1:`flux-submit`
