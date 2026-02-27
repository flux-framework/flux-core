================
flux-modprobe(1)
================


SYNOPSIS
========

| **flux** **modprobe** **load** [*--dry-run*] *MODULE* [*MODULE* ...]
| **flux** **modprobe** **remove** [*--dry-run*] *MODULE* [*MODULE* ...]
| **flux** **modprobe** **list-dependencies** [*-f*] *MODULE*
| **flux** **modprobe** **run** [*-v*] [*--show-deps*] [*--dry-run*] *FILE*
| **flux** **modprobe** **rc1** [*-v*] [*--timing*] [*--show-deps*] [*--dry-run*] *FILE*
| **flux** **modprobe** **rc3** [*-v*] [*--show-deps*] [*--dry-run*] *FILE*


DESCRIPTION
===========

.. program:: flux modprobe

:program:`flux modprobe` orchestrates module loading and unloading and
the execution of other startup and shutdown tasks using declarative
configuration. Modules and their precedence relationships are described
in a TOML configuration file (detailed in :ref:`modprobe_modules_conf`
below), and other startup and shutdown work is managed in Python rc files
(see the :ref:`modprobe_rc` section below).

:program:`flux modprobe` reads information about modules and tasks from
two sources, found in the ``modprobe`` subdirectory under the Flux
configuration dir (typically ``/etc/flux``):

 - ``modprobe.toml`` and ``modprobe.d/*.toml`` describe modules and their
   dependency and precedence relationships along with other module
   information. See :ref:`modprobe_modules_conf` for more details.
 - Startup and shutdown tasks are contained in ``rc1.py`` and ``rc3.py``
   and may be extended with files in ``rc1.d/*.py`` and ``rc3.d/*.py``.
   See :ref:`modprobe_rc` for more information.

Most users will not use :program:`flux modprobe` directly, but subcommands
and their usage is described in :ref:`modprobe_commands` for reference.

.. _modprobe_commands:

COMMANDS
========

load
----

.. program:: flux modprobe load

.. option:: --dry-run

  Do not actually run anything, but print the tasks that would be run.

:program:`flux modprobe load` loads one or more broker modules and
all their dependencies. Note that broker modules can be loaded by
service name instead of module name e.g.::

 $ flux modprobe load sched

loads the currently configured scheduler module.

This command will only load modules that are not already loaded, so it
may be used to ensure a given set of modules are loaded. If all target
modules and their dependencies are loaded, then an informational message
is printed that there was nothing to do and the program exits with success.


remove
------

.. program:: flux modprobe remove

.. option:: --dry-run

  Do not actually run anything, but print the tasks that would be run.

:program:`flux modprobe remove` unloads one or more broker modules, including
any modules that are unused after the target modules are removed.

The special name ``all`` may be used to unload all modules.
:program:`flux modprobe remove` will refuse to remove a module if the
module is still in use by other modules.

run
---

.. program:: flux modprobe run

:program:`flux modprobe run` takes the path to a modprobe rc file on
the command line and runs it. This is mainly useful for testing.

.. option:: --dry-run

  Do not actually run anything, but print the tasks that would be run.

.. option:: -v, --verbose

  Be more verbose.

.. option:: --show-deps

  Display a Python dictionary of tasks to predecessor list and exit.

show
----

.. program:: flux modprobe show

:program:`flux modprobe show` dumps the configuration information for a
module or service.

.. option:: -S, --set-alternative=NAME=MODULE

Set the alternative for service NAME to module MODULE.

.. option:: -D, --disable=NAME

Force disable module NAME.

list-dependencies
-----------------

.. program:: flux modprobe list-dependencies

Display a dependency graph for a given module or service name.

.. option:: -S, --set-alternative=NAME=MODULE

Set the alternative for service NAME to module MODULE.

.. option:: -D, --disable=NAME

Disable module NAME.

.. option:: -f, --full

Normally :program:`flux modprobe list-dependencies` suppresses the output
of duplicate dependencies. With the :option:`--full` option, duplicates
are displayed.

rc1
---

.. program:: flux modprobe rc1

:program:`flux modprobe rc1` runs the default Flux rc1 run script, typically
located at ``/etc/flux/modprobe/rc1.py`` plus any extra Python rc files
found in ``/etc/flux/modprobe/rc1.d/*.py``. This command is typically called
from Flux's rc1 script.

.. option:: --timing

Dump task timing information to the KVS.

.. option:: --show-deps

Display a dictionary of tasks to predecessor list and exit.

.. option:: -v, --verbose

Be more verbose.

.. option:: --dry-run

Print what would be run without doing anything. This may be useful for
checking expected rc1 behavior after making changes or adding files to
``/etc/flux/modprobe/rc1.d``.

rc3
---

.. program:: flux modprobe rc3

:program:`flux modprobe rc3` unloads all broker modules and executes
defined shutdown tasks from ``/etc/flux/modprobe/rc3.py`` and
``etc/flux/modprobe/rc3.d/*.py``.

.. option:: -v, --verbose

Be more verbose.

.. option:: --dry-run

Print what would be run without doing anything. This may be useful for
checking expected rc3 behavior after making changes or adding files to
``/etc/flux/modprobe/rc3.d``.


.. _modprobe_modules_conf:

MODULE CONFIG
=============

:program:`flux modprobe` loads information about Flux modules from the
``modules`` array in ``modprobe.toml``.

Module configuration may be extended by dropping new TOML files into a
``modprobe.d`` directory in the modprobe search path. The default search
path is::

  $datadir/flux/modprobe:$sysconfdir/flux/modprobe

which is typically::

  /usr/share/flux/modprobe:/etc/flux/modprobe

Packages should install files under ``/usr/libexec/flux/modprobe`` while
site updates and modifications go under ``/etc/flux/modprobe``.

The default search path can be overridden using :envvar:`FLUX_MODPROBE_PATH`
or extended using :envvar:`FLUX_MODPROBE_PATH_APPEND`

Each entry in the ``modules`` array supports the following keys:

**name**
   (required, string) The module name. This will be the target of module
   load and remove requests. If there is a name collision between entries,
   then the last entry loaded will be used.

**module**
   (optional, string) The module to load if different from *name*

**provides**
   (optional, array of string) List of service names this module provides,
   e.g. a scheduler module will provide the "sched" service. If multiple
   modules provide the same service, they are sorted first by *priority*
   then load order, and the last entry takes precedence. This may be
   overridden by configuration, however.

**priority**
   (optional, integer) The relative priority of this module and its provided
   services. Since alternatives are sorted by priority then load order, a
   module may be given a high priority to ensure it overrides any alternatives
   for a given service name. The default priority is 100.

**args**
   (optional, array of string) Array of default module arguments.

**exec**
   (optional, boolean) If true, run module as a separate process instead
   of as a thread in the Flux broker process. Default: false.

**ranks**
   (optional, str) The set of ranks on which this module should be loaded by
   default. May either be an RFC 22 idset string, or a string prefixed with
   ``>`` or ``<`` followed by a single integer. (for example, ``">0"`` to
   load a module all ranks but rank 0.)

**requires**
   (optional, array of string) An array of services this module requires. That
   is, if this module is loaded, the services in *requires* will also be
   loaded. If this module also has to be loaded after any of its required
   modules, then add them to the *after* array as well.

**after**
   (optional, array of string) An array of modules that must be loaded before
   this module. If this module also requires the service or module it is loaded
   after, then the module/service name should also appear in *requires*.

**needs**
   (optional, array of string) An array of module/services which are required
   for this module to be loaded. In other words, if a module appearing in
   *needs* is disabled, then this module will be disabled as well.

**needs-config**
   (optional, array of string) An array of configuration keys in dotted key
   form which are required for this module to be loaded. That is, if this
   config key is not set, then the module is disabled.

**needs-attrs**
   (optional, array of string) An array of broker attributes which are required
   to be set for this module to be loaded.

**needs-env**
   (optional, array of string) An array of environment variables which are
   required to be set for this module to be loaded. That is, if any of the
   specified environment variables are not set, then the module is disabled.
   Note that the broker environment is checked if a variable is not found in
   the local environment, since the rc1 environment may differ from the broker
   environment.

The ``modprobe.toml`` config file also supports the following keys:

**alternatives**
   (optional) A table of keys that force selection of module alternatives, e.g. ::

     alternatives.sched = "sched-simple"

**<name>.**
   (optional) Table of updates for individual modules. The module must have
   been previously defined. This allows a site to update module configuration
   by dropping a file into the ``modprobe.d`` directory., e.g. ::

     feasibility.ranks = "0,5,7"

Module configuration may also be extended via the ``modules`` table in the
broker config (see :man5:`flux-config`). This approach is useful to modify
configuration for individual Flux instances. Each entry in the ``modules``
table should itself be a table of updates for a named module, for example:

.. code-block:: toml

  [modules]
  feasibility.ranks = "0,1"

Or, to enable the same config for an instance launched by
:command:`flux alloc`:

.. code-block:: console

  $ flux alloc -N16 --conf=modules.feasibility.ranks="0,1"

.. _modprobe_rc:

MODPROBE RC
===========

:command:`flux modprobe` supports execution of arbitrary tasks in addition
to module loading and removal with the :command:`run`, :command:`rc1`, and
:command:`rc3` subcommands. These tasks are defined in Python rc files
using the *task* decorator imported from :py:func:`flux.modprobe.task`, e.g.:

.. code:: python

  from flux.modprobe import task

  @task("test")
  def test_task(context):
    print("running test task")

Similar to module configuration, tasks and setup may be added to the default
:command:`rc1` and :command:`rc3` by dropping new Python files into a
``rcX.d`` directory in the modprobe rc search path. The default search
path is::

  $libexecdir/flux/modprobe:$sysconfdir/flux/modprobe

which, e.g. for ``rc1`` would typically be:

  /usr/libexec/flux/modprobe/rc1.d:/etc/flux/modprobe/rc1.d

Packages should install files under ``/usr/libexec/flux/modprobe`` while
site updates and modifications go under ``/etc/flux/modprobe``.

The default search path can be overridden using :envvar:`FLUX_MODPROBE_PATH`
or extended using :envvar:`FLUX_MODPROBE_PATH_APPEND`

The *task* decorator requires a task *name*, and in addition supports the
following optional arguments:

**ranks**
   (required, str) A string indicating on which broker ranks this task should
   be invoked. It may be an RFC 22 idset string, or a single integer prefixed
   with a conditional such as ``<`` or ``>``, or the string "all" (the default).

**requires**
   (optional, list) A list of task or module names this task requires. This is
   used to ensure required tasks are active when activating another task.
   It does not indicate that this task will necessarily be run before or after
   the tasks it requires. (See ``before`` or ``after`` for those features)

**needs**
   (optional, list) A list of tasks or modules this task needs. Disables this
   task if any module/task in *needs* is disabled.

**provides**
   (optional, list) A list of string service names this task provides. Can
   be used to set up alternative tasks for a given name (Mostly used with
   modules)

**before**
   (optional, list) A list of tasks or modules that this task must run before.
   During startup (``run`` or ``rc1``), this task runs before the listed modules
   are loaded. During shutdown (``rc3``), this task runs before the listed
   modules are removed.

   The special value ``"*"`` ensures this task runs before all other named
   tasks (except ``setup()`` and other tasks that also specify ``before=["*"]``).
   Multiple tasks using ``before=["*"]`` run in parallel with no guaranteed
   ordering. If a task uses ``before=["*"]`` then the ``after`` list must be empty.

   .. note::
      A task with ``before=["*"]`` cannot be listed in another task's
      ``before`` list, as this would create a circular dependency.

**after**
   (optional, list) A list of tasks or modules that this task must run after.
   During startup (``run`` or ``rc1``), this task runs after the listed modules
   are loaded. During shutdown (``rc3``), this task runs after the listed
   modules are removed.

   The special value ``"*"`` ensures this task runs after all other named
   tasks (except other tasks that also specify ``after=["*"]``). Multiple tasks
   using ``after=["*"]`` run in parallel with no guaranteed order. If a task uses
   ``after=["*"]`` then the ``before`` list must be empty.

   .. note::
      A task with ``after=["*"]`` cannot be listed in another task's
      ``after`` list, as this would create a circular dependency.

**needs_attrs**
   (optional, list) A list of broker attributes on which this task depends.
   If the attribute begins with the character ``!``, then this task will only
   be enabled if the named attribute is not set.

**needs_config**
   (optional, list) A list of config keys on which this task depends. If a
   key is prefixed with the character ``!``, then this task will only be
   enabled if that config key is not set.

**needs_env**
   (optional, list) A list of environment variables on which this task depends.
   If a variable is prefixed with the character ``!``, then this task will only
   be enabled if that environment variable is not set. Note that the broker
   environment is checked if a variable is not found in the local environment,
   since the rc1 environment may differ from the broker environment.

Example

.. code:: python

   from flux.modprobe import task

   # Declare a task to be run on rank 0 only
   # after the kvs module is loaded and only if
   # the foo.do-thing config key is present:
   @task(
       "foo-do-thing",
       ranks="0",
       needs=["kvs"],
       after=["kvs"],
       needs_config=["foo.do-thing"]
   )
   def do_thing(context):
       # do something with kvs


If a *setup* function is provided, it is always run just after loading
the rc file, and may be used to schedule modules to be loaded, modify
configuration etc:

.. code:: python

  def setup(context):
     context.setopt("my-module", "arg=value")
     context.load_modules(["my-module")

In both the *setup* function and *task* decorator, an instance of the
:py:class:`flux.modprobe.Context` class is passed as the sole argument.  This
context can be used to set the list of modules to load, add or overwrite
module arguments, set and get arbitrary shared data between tasks, query
broker configuration and attributes, and even run shell commands.

**context.print(self, *args)**
   Print a message if modprobe is in verbose output mode

**context.handle**
   Return a per-thread Flux handle created on demand

**context.rank**
   Return the current broker rank

**context.set(key, value)**
   Set arbitrary data at key for future use using ``context.get()``

**context.get(key, default=None)**
   Get arbitrary data set by other tasks with optional default value

**context.attr_get(attr, default=None)**
   Get broker attribute. If attribute is not set, return ``default``.

**context.conf_get(key, default=None)**
   Get broker config value in dotted key form. Returns ``default`` if ``key``
   is not set.

**context.rpc(topic, *args, **kwargs)**
   Send an RPC using the internally provided Flux handle.

**context.setopt(module, options, overwrite=False)**
   Append option to module options for ``module``. ``options`` may contain
   multiple options separated by whitespace. If ``overwrite`` is ``True``,
   then options replace all existing options instead of appending.

**context.getopts(name, default=None, also=None)**
   Get module options for module ``name``. If ``also`` is a list, then
   append any defined module options for those names as well.

**context.bash(command)**
   Execute ``command`` under ``bash -c``.

**context.load_modules(modules)**
   Append a list of modules to the set of modules to load by name.

**context.remove_modules(modules=None)**
   Set a list of modules to remove. If ``modules`` is None, remove all
   currently loaded modules.

**context.enable(name)**
  Force enable a module, service, or task, overriding all conditionals that
  may cause it to currently be disabled. Note: This will not also enable
  dependencies of ``name``.

**context.getenv(var, default=None)**
   Return the value of environment variable *var*, or *default* if not set.
   The local environment is checked first, falling back to the broker
   environment via the ``broker.getenv`` RPC if necessary. This is useful
   because the broker may filter some variables from the rc1 and rc3
   environments, such as those set by foreign resource managers or
   launchers. Broker environment results are cached so the RPC is only
   issued once per variable.

**context.setenv(name_or_env, value=None)**
   Set or unset one or more environment variables in both the local process
   and the broker. Variables set via this method that are not in the
   broker's env blocklist will be inherited by rc2 and rc3.

   *name_or_env* may be a string variable name, in which case *value* is
   the string value to set, or a ``dict`` mapping variable names to
   values for setting multiple variables at once. A value of ``None``
   causes the named variable to be unset.

   Raises ``ValueError`` if any *value* is not a string or ``None``.
   Raises ``OSError`` if the ``broker.setenv`` RPC fails, for example if
   a variable name is empty or contains ``=``.

   .. note::
      If the variable only needs to be visible within the current process
      and does not need to propagate to rc2 or rc3, use ``os.environ``
      directly instead.


RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man1:`flux-module`
