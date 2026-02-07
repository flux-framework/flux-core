=====================
flux-shell-plugins(7)
=====================

DESCRIPTION
===========

The Flux job shell supports external and builtin plugins which implement
most advanced job shell features. This manual serves as a developer's guide
for creating custom shell plugins, documenting the plugin system architecture,
available plugin callbacks, and providing examples of plugin development
in both C and Lua.

.. note::
  Job shell plugins should be written with the assumption their access
  to Flux services may be restricted as a guest.

OVERVIEW
========

The shell's plugin architecture enables:

**Feature Implementation**: Most shell features (I/O redirection, CPU/GPU
affinity, PMI, signal handling) are implemented as plugins rather than
hardcoded in the shell core.

**Customization**: Users can override builtin behavior by loading replacement
plugins with the same name.

**Extension**: Custom plugins can add new capabilities like specialized
task mapping, custom environment setup, or integration with external tools.

**Multiple Languages**: Plugins can be written in C/C++ for performance
and full API access, or Lua for rapid development and simpler logic.

PLUGIN ARCHITECTURE
===================

Shell plugins are loaded into a plugin stack by name, where the last loaded
name wins. This allows users to override builtin plugins by loading an
alternate plugin with the same name at runtime.

Plugins register callback functions that are invoked at defined points during
shell execution. These callbacks are identified by topic strings, and plugins
can subscribe to topics using :linux:man7:`glob` patterns, allowing a single
callback to handle multiple related events.

Plugin Loading
--------------

Plugins can be loaded in several ways:

1. **Builtin plugins** are automatically loaded by the shell at startup.
   These provide core functionality like affinity, I/O, and PMI.

2. **System plugins** are loaded via the default system initrc file
   (``$sysconfdir/flux/shell/initrc.lua``), which contains:

   .. code-block:: lua

     -- Load all *.so plugins from plugin.searchpath
     plugin.load { file = "*.so", conf = {} }

     -- Source all rc files under shell.rcpath/lua.d/*.lua
     shell.source_rcpath ("*.lua")

   This loads compiled plugins from the default shell pluginpath and Lua
   rc scripts/plugins from the default shell rcpath (e.g.
   ``/etc/flux/shell/lua.d/*.lua``)

   The default value for the shell pluginpath can be queried via the
   :man1:`flux-config` utility:

   .. code-block:: console

     $ flux config builtin shell_pluginpath
     /usr/lib64/flux/shell/plugins

   It is typically set to ``$libdir/flux/shell/plugins``. This value may be
   overridden via the ``conf.shell_pluginpath`` broker attribute (see
   :man7:`flux-broker-attributes`), or in the shell initrc by direct
   manipulation of ``plugin.searchpath``.

   Lua rc extensions and plugins are loaded by ``shell.source_rcpath()``
   which defaults to:

   .. code-block:: none

     <path to rcfile>/lua.d/*.lua:$FLUX_SHELL_RC_PATH

   Since the default ``initrc.lua`` is typically installed in
   ``/etc/flux/shell`` and ``$FLUX_SHELL_RC_PATH`` is empty, Lua rc scripts
   and plugins will be loaded from ``/etc/flux/shell/lua.d/*.lua`` by default.

3. **User provided plugins** can be loaded via a user initrc file specified
   with the ``userrc`` shell option:

   .. code-block:: console

     $ flux run -o userrc=/path/to/my-initrc.lua ./myapp

   where ``my-initrc.lua`` would contain one or more ``plugin.load``
   or ``plugin.register`` directives.

   Note that a user may also override the default shell ``initrc.lua``
   on the command line via the ``initrc=FILE`` option, in which case
   the loading of system plugins may be bypassed.

Plugin Stack Behavior
----------------------

The plugin stack uses a "last wins" strategy for plugin names. When a plugin
with name *N* is loaded:

- If a plugin named *N* already exists, it is replaced
- All callbacks from the old plugin are removed
- New callbacks from the replacement plugin are registered

This allows users to override specific builtin plugins without modifying
system files. For example, to replace the builtin cpu-affinity plugin:

.. code-block:: lua

  -- In userrc
  plugin.load { file = "/path/to/my-affinity.so" }

The custom plugin must use the same name
(``FLUX_SHELL_PLUGIN_NAME "cpu-affinity"``) to override the builtin.

.. note::

   Replacement of a plugin with a duplicate name occurs *after* the
   existing plugin has been loaded and initialized, so actions that
   occur in builtin or previously loaded plugin initialization callbacks
   cannot be overridden by a plugin with the same name.

PLUGIN CALLBACKS
================

Shell plugins register callback functions that are invoked at defined
points during shell execution. The callbacks are organized into several
categories based on when they are invoked in the shell lifecycle.

For an overview of the shell lifecycle, see the OPERATION section of
:man1:`flux-shell`.

Callback Execution Order
------------------------

Understanding callback ordering helps ensure plugins interact correctly:

1. Callbacks within the same topic are called in the order plugins were loaded
2. Builtin plugins are loaded first, then system plugins, then user plugins
3. Later plugins can override earlier plugins by using the same name
4. Callbacks can return -1 to signal an error, which typically fails the job
5. Some callbacks (like taskmap) can modify arguments for subsequent callbacks

Initialization Callbacks
-------------------------

**shell.connect**
  Called immediately after the shell connects to the local Flux broker,
  before jobspec and resource information are fetched.

  **Availability**: Only builtin plugins (runs before initrc is loaded)

  **Use cases**: Early initialization requiring a broker connection but
  before job metadata is available.

  **Context**: Minimal shell state available.

**shell.init**
  Called after the shell has finished fetching and parsing the jobspec and
  resource set R from the KVS, but before any tasks are started. This is
  the primary initialization point for most plugins.

  **Use cases**: Reading shell options, examining job resources, initializing
  plugin state, setting up data structures.

  **Available data**: Full jobspec, resource set R, shell info (rank, size,
  ntasks), shell options.

  **Common operations**:

  - :man3:`flux_shell_getopt` - Read shell options
  - :man3:`flux_shell_get_info` - Get shell metadata
  - :man3:`flux_shell_get_rank_info` - Get information specific to a shell rank
  - :man3:`flux_shell_aux_set` or ``flux_plugin_aux_set()`` - Store plugin state

**shell.post-init**
  Called after the shell initialization barrier has completed, but before
  starting any tasks.

  **Use cases**: Operations that require all shells to have completed
  ``shell.init``, or that must occur after all other plugin ``shell.init``
  callbacks.

  **Context**: All shells have loaded plugins and completed initialization.
  No tasks have started yet.


Task Lifecycle Callbacks
-------------------------

**task.init**
  Called for each task after the task info has been constructed but before
  the task is executed. This is the appropriate place to modify task
  environment variables or other pre-execution task properties.

  **Use cases**: Setting task-specific environment variables, modifying
  task command arguments, setting up per-task affinity, logging task startup.

  **Available data**: Task rank, local ID, assigned resources, environment,
  command arguments.

  **Common operations**:

   - :man3:`flux_shell_current_task` - Get current task handle
   - :man3:`flux_shell_task_cmd` - Get/modify task command
   - ``flux_cmd_setenvf()`` - Set per-task environment variables
   - :man3:`flux_shell_task_info_unpack` - Get task metadata

  **Context**: Runs in parent shell process before fork. Environment and
  command modifications affect the task that will be forked.

**task.exec**
  Called for each task after fork, just before :linux:man2:`execve` is called.
  This callback runs in the child process and should be used sparingly.

  **Use cases**: Final process context modifications (e.g., OOM score
  adjustment, ptrace setup), debug tracing.

  **Available data**: Task info, environment (read-only at this point).

  **Context**: Runs in child process after fork, before exec. Very limited
  environment - avoid heavy operations or complex error handling.

  .. warning::
     This callback runs in the child process after fork. The shell logging
     facility may not be fully available. Operations here should be minimal
     and fast. Errors are difficult to report properly.

**task.fork**
  Called for each task after it is forked from the parent process. This
  callback runs in the parent shell process.

  **Use cases**: Recording task PIDs, setting up monitoring, parent-side
  task tracking, initializing per-task watchers.

  **Available data**: Task info including PID.

  **Context**: Runs in parent shell process after fork. Task is running.

  **Common operations**:

   - :man3:`flux_shell_current_task` - Get current task handle
   - :man3:`flux_shell_task_subprocess` - Get task subprocess handle
   - :man3:`flux_shell_task_info_unpack` - Get task metadata

**task.exit**
  Called for each task after it exits. This is where plugins can examine
  task exit status and take action based on how tasks terminated.

  **Use cases**: Logging exit status, handling failures, cleaning up per-task
  resources, generating exceptions based on exit conditions, collecting
  per-task statistics.

  **Available data**: Task info, wait status, exit code, flag if task
  was killed due to signaled.

  **Common operations**:

   - :man3:`flux_shell_task_info_unpack` - Extract wait_status, exitcode,
     and signaled components of task info

Shell Lifecycle Callbacks
--------------------------

**shell.start**
  Called after all local tasks have been started. The shell "start" barrier
  is called just after this callback returns, ensuring all shells have
  started their tasks before proceeding.

  **Use cases**: Operations that need all tasks running, starting monitoring,
  initiating performance tracking, logging job start.

  **Context**: All local tasks are running but may not have started executing
  yet. After the callback returns, a barrier ensures all shells have reached
  this point.

**shell.finish**
  Called after all local tasks have exited but before the shell exits the
  reactor. This callback runs while the reactor is still active, making it
  suitable for cleanup operations requiring an active event loop.

  **Use cases**: Asynchronous cleanup, sending completion messages,
  collecting final metrics, operations requiring Flux RPC calls, writing
  final output.

  **Context**: Reactor is still running. Asynchronous operations are possible.
  All local tasks have exited.

  **Common operations**:

   - :man3:`flux_shell_task_first`, :man3:`flux_shell_task_next` to iterate
     all tasks.

  **Example scenario**: An I/O plugin flushes remaining output buffers and
  sends final EOF messages to the job's KVS namespace.

**shell.exit**
  Called after the shell has exited the reactor. This is the final callback
  before the shell process terminates. Use this only for final synchronous
  cleanup that doesn't require an active event loop.

  **Use cases**: Final synchronous cleanup, closing file descriptors,
  freeing memory, final logging.

  **Context**: Reactor has stopped. Asynchronous operations should be avoided.
  The shell is about to exit.

  .. warning::
     The reactor is no longer active. Do not attempt RPC calls or other
     asynchronous operations. This callback is for final cleanup only.

.. note::
   The distinction between ``shell.finish`` and ``shell.exit`` is important:
   ``shell.finish`` runs while the reactor is active (suitable for asynchronous
   cleanup and time-sensitive operations), while ``shell.exit`` runs after
   the reactor stops (suitable only for final synchronous cleanup).

Special Purpose Callbacks
--------------------------

**taskmap.SCHEME**
  Called when a taskmap scheme *SCHEME* is requested via the ``taskmap``
  shell option or the corresponding :option:`flux submit --taskmap` option.
  Plugins can register a ``taskmap.*`` callback to provide custom task
  mapping schemes beyond the built-in ``block``, ``cyclic``, ``hostfile``,
  and ``manual`` schemes.

  **Input**: The callback receives the default block taskmap in the
  "taskmap" input argument.

  **Output**: Should return a new taskmap RFC 43 taskmap string in the
  output arguments.  The format is a semicolon-separated list of node
  ranks for each task.

  **Timing**: This callback is invoked before ``shell.init``, during taskmap
  construction.

  **Use cases**: Implementing custom task placement strategies

  .. note::
     This callback requires returning data via output arguments and therefore
     cannot be used with Lua plugins, which do not yet support returning
     out arguments.

**mustache.render.TEMPLATE**
  Called when a mustache template *TEMPLATE* needs to be rendered by the
  shell. This allows plugins to implement custom mustache templates for
  use in environment variables and job arguments.

  **Timing**: Invoked for each task before it starts to expand command
  arguments, for any environment variable containing a mustache template, or
  when a plugin calls :man3:`flux_shell_mustache_render`.

  **Use cases**: Providing custom template expansions (e.g., job-specific
  paths, dynamically computed values, external data).

  .. note::
     This callback requires returning data via output arguments and therefore
     cannot be used with Lua plugins, which do not yet support returning
     out arguments.

Event and Exception Callbacks
------------------------------

**shell.log**
  Called by the shell logging facility when a shell component posts a log
  message. This allows plugins to intercept, filter, or redirect log messages.

  .. note::
     Code in ``shell.log`` callbacks should not call any of the shell
     logging functions.

  **Use cases**: Custom logging backends, filtering log messages,
  redirecting logs to external systems, implementing structured logging.

  **Available data**: Log level, message, source component.

**shell.log-setlevel**
  Called by the shell logging facility when a request to set the shell
  loglevel is made. This allows plugins to respond to dynamic log level
  changes.

  **Use cases**: Adjusting plugin verbosity dynamically, enabling/disabling
  debug output.

  **Example**: A monitoring plugin increases its sampling rate
  when the shell log level is raised to debug.

**shell.lost**
  Called when the job receives a lost-shell exception, indicating that
  another shell in the job has failed (typically due to a node crash).

  **Available data**: The callback context contains the affected shell rank
  (``shell_rank``) and exception ``severity``.

  **Use cases**: Responding to shell failures, adjusting tracking of current
  effective job size, disabling features that require all shells.

  **Example**: The output plugin uses this callback to adjust the number of
  remote shells from which it expects EOF when a shell is lost.

**shell.resource-update**
  Called when the job's resource set R is updated. This can occur when the
  resource set expiration is modified (e.g., job time limit extension).

  **Use cases**: Reconfiguring plugin state after resource set modification,
  adjusting timers, updating resource tracking.

  **Example**: The signal plugin can be configured to send a signal to the
  job a configurable period before the job expires. It uses this callback
  to adjust its internal timer when the job expiration is updated.

**shell.reconnect**
  Called after the shell reconnects its handle to the local Flux broker
  (e.g., due to a broker restart). This allows plugins to re-establish
  connections or refresh state after broker restarts.

  **Use cases**: Re-registering services, refreshing handles, verifying
  connection state, re-subscribing to events.

  **Context**: The shell's broker connection has been re-established but
  plugin-specific connections or subscriptions may need refresh.

  **Example**: The plugin handling output to the KVS clears state for
  any pending responses which may have been lost during the reconnect.

Custom Callbacks
----------------

Plugins may also call into the plugin stack to create new callbacks
at runtime using :man3:`flux_shell_plugstack_call`. This allows plugins
to define their own extension points for other plugins to use.

**Use cases**: Creating plugin ecosystems, defining hook points for
optional extensions, coordinating between related plugins.

**Example** The builtin ``exception`` plugin watches the job's
eventlog for node failure exceptions and calls into other plugins via

.. code-block::

  flux_plugstack_call (shell, "shell.lost", args);

DEVELOPING C PLUGINS
====================

The C API is the primary interface for shell plugin development. It offers
maximum performance and full access to Flux and shell APIs. Plugins
are defined using the Flux standard plugin format and should export a
single symbol ``flux_plugin_init()``.

Building and Testing
--------------------

Compile plugins as a shared library. For an example simple plugin:
(This will work for most examples in this document)

.. code-block:: console

  $ gcc -shared -fPIC -o myplugin.so myplugin.c

Simple plugins can be tested within a local test instance of Flux.
Set the ``conf.shell_pluginpath`` for the test instance to the directory
with your plugin, and the default shell initrc will automatically load it:

.. code-block:: console

  $ flux start -s2 -S conf.shell_pluginpath=$(pwd)
  $ flux run -N2 -o myplugin.enable=1 test-app

Alternatively, test by loading explicitly via a userrc:

.. code-block:: console

  $ cat > test-rc.lua << 'EOF'
  plugin.load { file = "/path/to/myplugin.so" }
  EOF
  $ flux run -o userrc=test-rc.lua myapp

Developing and testing plugins in a test instance of Flux allows for
faster edit-compile-test cycles, better support for automated testing
(:man1:`flux-start` can be invoked directly from make check), and isolation
of testing and changes from production systems.

Usage examples of working plugins throughout this document will assume
that you have saved the example code to a file, compiled it as instructed
above, and are within a Flux instance configured such that the ``.so``
will be found by default.

Basic Plugin Structure
----------------------

A minimal C plugin follows this structure:

.. note::

  Examples below omit error checking for brevity. Production code should
  check all return values and handle errors appropriately.

Plugins must define ``FLUX_SHELL_PLUGIN_NAME`` before including the
shell plugin header ``<flux/shell.h>``. This is necessary for shell logging
functions to work properly. Failure to define ``FLUX_PLUGIN_NAME`` will
result in a compile-time error.

.. code-block:: c

  /* FLUX_SHELL_PLUGIN_NAME must be defined before flux/shell.h include
   */
  #define FLUX_SHELL_PLUGIN_NAME "test"

  #include <flux/shell.h>

  static int my_init_cb (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *data)
  {
      flux_shell_t *shell = flux_plugin_get_shell (p);
      shell_log ("Plugin initialized");

      /* Do something at shell initialization */

      return 0;
  }

  int flux_plugin_init (flux_plugin_t *p)
  {
      return flux_plugin_add_handler (p,
                                      "shell.init",
                                      my_init_cb,
                                      NULL);
  }

The ``flux_plugin_init()`` function is called when the plugin is loaded.
It should register callback handlers using ``flux_plugin_add_handler()``
for the plugin callback topics of interest.

.. note::
   ``flux_plugin_init()`` is not called for builtin shell plugins. If
   a dynamically loaded plugin wishes to set shell options to influence
   a builtin plugin's operation (e.g., to disable it), it should do so
   in ``flux_plugin_init()`` to guarantee the shell option is set before
   the builtin attempts to read it. Use :man3:`flux_shell_setopt` to set
   options.

Callback Handlers
-----------------

Callback handlers receive four parameters:

- ``flux_plugin_t *p`` - The plugin handle
- ``const char *topic`` - The callback topic that triggered this handler
- ``flux_plugin_arg_t *args`` - callback specific in and out arguments
- ``void *data`` - User data pointer passed to ``flux_plugin_add_handler()``

Handlers should return 0 on success, -1 on error. Returning -1 will
typically cause the shell to fail the job by raising a fatal exception.

Accessing Shell Information
----------------------------

Plugins can access shell information using the shell API:

.. code-block:: c

  flux_shell_t *shell = flux_plugin_get_shell (p);
  
  // Get current shell info:
  int rank, size, ntasks;
  if (flux_shell_info_unpack (shell,
                               "{s:i s:i s:i}",
                               "rank", &rank,
                               "size", &size,
                               "ntasks", &ntasks) < 0) {
      shell_log_errno ("flux_shell_info_unpack");
      return -1;
  }
  
  // Log messages at various levels
  shell_log ("shell %d/%d starting", rank, size);
  shell_debug ("debug message");  // Only visible with verbose
  shell_log_error ("error message");
  
  // Unpack shell options:
  int verbose = 0;
  if (flux_shell_getopt_unpack (shell, "verbose", "i", &verbose) < 0)
      shell_log_errno ("flux_shell_getopt_unpack");
  
  // Check for specific option existence:
  if (flux_shell_getopt (shell, "opt.enabled") != NULL)
      shell_log ("myopt.enabled is set");

Working with Tasks
------------------

Task-related callbacks can access task information via
:man3:`flux_shell_current_task`:

.. code-block:: c

  static int task_init_cb (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *args,
                           void *data)
  {
      flux_shell_t *shell = flux_plugin_get_shell (p);
      flux_shell_task_t *task = flux_shell_current_task (shell);
      
      int rank, localid;
      if (flux_shell_task_info_unpack (task,
                                       "{s:i s:i}",
                                       "rank", &rank,
                                       "localid", &localid) < 0) {
          shell_log_errno ("task_info_unpack");
          return -1;
      }
      
      // Set task environment variable
      flux_cmd_t *cmd = flux_shell_task_cmd (task);
      if (flux_cmd_setenvf (cmd, 1, "TASK_RANK", "%d", rank) < 0) {
          shell_log_errno ("flux_cmd_setenvf");
          return -1;
      }
      
      shell_debug ("configured task %d (local %d)", rank, localid);
      
      return 0;
  }

Storing Plugin State
--------------------

Plugins often need to maintain state across callbacks.
State can be stored in the shell or plugin handle, e.g.:

.. code-block:: c

  struct mydata {
      int counter;
      char *config;
  };

  static void mydata_free (void *data)
  {
      struct mydata *d = data;
      free (d->config);
      free (d);
  }

  static int shell_init_cb (flux_plugin_t *p, ...)
  {
      flux_shell_t *shell = flux_plugin_get_shell (p);
      struct mydata *data = calloc (1, sizeof (*data));
      
      data->counter = 0;
      data->config = strdup ("default");
      
      // Store in plugin (freed when plugin unloads)
      flux_plugin_aux_set (p, "mydata", data, mydata_free);
      
      // Or store in shell (available to other callbacks, but
      // use a plugin-specific prefix to avoid collisions)
      flux_shell_aux_set (shell, "myplugin::mydata", data, mydata_free);
      
      return 0;
  }

  static int task_init_cb (flux_plugin_t *p, ...)
  {
      flux_shell_t *shell = flux_plugin_get_shell (p);
      struct mydata *data = flux_shell_aux_get (shell, "myplugin::mydata");
      
      data->counter++;
      shell_log ("task count: %d", data->counter);
      
      return 0;
  }

Example: Environment Variable Plugin
-------------------------------------

This plugin sets custom environment variables for each task. Global
environment variables affecting all tasks may also be set via
:man3:`flux_shell_setenvf`.

.. literalinclude:: example/env-plugin.c
  :language: c

Usage:

.. code-block:: console

  $ flux run --label-io -n4 printenv MY_LOCAL_RANK
  3: 3
  2: 2
  1: 1
  0: 0

Example: Custom Taskmap Plugin
-------------------------------

This plugin implements a custom reverse task mapping scheme:

.. literalinclude:: example/taskmap-reverse.c
  :language: c

Usage:

.. code-block:: console

  $ flux run -N4 -n4 --taskmap=reverse --label-io flux getattr rank
  3: 0
  0: 3
  1: 2
  2: 1

Example: Task Exit Monitor
---------------------------

This plugin monitors task exits and logs failures:

.. literalinclude:: example/exit-monitor.c
  :language: c

Usage:

.. code-block:: console

  $ flux run -n1 false
  0.085s: flux-shell[0]: exit-monitor: task 0 exited with code 1
  flux-job: task(s) exited with exit code 1


Example: Configuration Plugin
------------------------------

This plugin demonstrates reading and using configuration options:

.. literalinclude:: example/config-test.c
  :language: c

Usage:

.. code-block:: console

  $ flux run -o config-test.mode=advanced -o config-test.level=2 true
  0.076s: flux-shell[0]: config-test: initialized with mode=advanced, level=2

Installation
------------

For plugins that should be loaded by default, install to a location
in the default shell plugin search path, which can be queried using
:linux:man1:`pkg-config`:

.. code-block:: console

  $ pkg-config --variable=fluxshellpluginpath flux-core
  /usr/lib64/flux/shell/plugins

or using :man1:`flux-config`:

.. code-block:: console

  $ flux config builtin shell_pluginpath
  /usr/lib64/flux/shell/plugins

Plugins may also be installed outside the default search path and
manually loaded via a Lua rc snippet when loading should be optional
or controlled via rc script logic:

.. code-block:: lua

  plugin.load { file = "/path/to/plugin.so" }
  -- or with configuration:
  plugin.load { file = "/path/to/plugin.so", conf = { foo = 42 } }

DEVELOPING LUA PLUGINS
======================

Lua plugins offer rapid development and can be written directly in shell
initrc files. See :man5:`flux-shell-initrc` for the complete Lua API
reference, examples, and limitations.

Basic Structure
---------------

Lua plugins are registered using the ``plugin.register()`` function:

.. code-block:: lua

  plugin.register {
    name = "myplugin",
    handlers = {
      {
        topic = "shell.init",
        fn = function ()
          shell.log ("Plugin initialized")
        end
      }
    }
  }

The ``handlers`` table is an array where each entry contains:

- ``topic`` - A topic glob string to match callback topics
- ``fn`` - A Lua function to call when the topic matches

Lua plugin callbacks are invoked at the same points in the shell lifecycle
as C plugin callbacks and use the same topic strings (``shell.init``,
``task.init``, ``task.exit``, etc.). The only difference is in how they
are registered: C plugins use ``flux_plugin_add_handler()``, while Lua
plugins use ``plugin.register()``.

Multiple handlers can be registered in a single plugin:

.. code-block:: lua

  plugin.register {
    name = "myplugin",
    handlers = {
      {
        topic = "shell.init",
        fn = function () 
          -- initialization
        end
      },
      {
        topic = "task.init",
        fn = function ()
          -- per-task setup
        end
      },
      {
        topic = "task.exit",
        fn = function ()
          -- per-task cleanup
        end
      }
    }
  }

Lua Plugin Limitations
-----------------------

Lua plugins have some limitations compared to C plugins:

- No direct access to Flux RPC calls
- Cannot register custom services
- Limited access to task subprocess details
- No access to low-level file descriptors
- Cannot implement callbacks that return output arguments:
  - ``taskmap.*`` callbacks (custom task mapping schemes)
  - ``mustache.render.*`` callbacks (custom template expansions)

For advanced features requiring full Flux API access or output argument
handling, use C plugins.

DEBUGGING
=========

Enable verbose logging:

.. code-block:: console

  $ flux run -o verbose=2 myapp

Test plugins in isolation:

.. code-block:: console

  $ flux start -s1 -S conf.shell_pluginpath=$(pwd)
  $ flux run -o userrc=test.lua myapp

Use :man3:`shell_log` for debug logging:

.. code-block:: c

  shell_log ("my_callback: rank=%d, value=%d", rank, value);

Run the job shell under valgrind:

.. code-block:: console

  $ cat >shell.sh <<EOF
  #!/bin/sh
  valgrind --leak-check=full $(flux config builtin shell_path) "\$@"
  EOF
  $ chmod +x shell.sh
  $ flux run -Sexec.job_shell=$(pwd)/shell.sh true

If working out of a build directory, prefix :command:`valgrind` with
:command:`libtool e`.

RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man1:`flux-shell`, :man5:`flux-shell-initrc`, :man7:`flux-shell-options`,
:man3:`flux_shell_add_completion_ref`
:man3:`flux_shell_add_event_context`
:man3:`flux_shell_add_event_handler`
:man3:`flux_shell_aux_set`
:man3:`flux_shell_current_task`
:man3:`flux_shell_getenv`
:man3:`flux_shell_get_flux`
:man3:`flux_shell_get_hostlist`
:man3:`flux_shell_get_hwloc_xml`
:man3:`flux_shell_get_info`
:man3:`flux_shell_get_jobspec_info`
:man3:`flux_shell_getopt`
:man3:`flux_shell_get_taskmap`
:man3:`flux_shell_killall`
:man3:`flux_shell_log`
:man3:`flux_shell_mustache_render`
:man3:`flux_shell_plugstack_call`
:man3:`flux_shell_rpc_pack`
:man3:`flux_shell_service_register`
:man3:`flux_shell_task_channel_subscribe`
:man3:`flux_shell_task_get_info`
:man3:`flux_shell_task_subprocess`
