==============
flux-module(1)
==============


SYNOPSIS
========

| **flux** **module** **load** [*--name*] [*--exec*] *module* [*args...*]
| **flux** **module** **reload** [*--name*] [*--exec*] [*--force*] *module* [*args...*]
| **flux** **module** **remove** [*--force*] *name*
| **flux** **module** **list** [*-l*]
| **flux** **module** **stats** [*-R*] [*--clear*] *name*
| **flux** **module** **debug** [*--setbit=VAL*] [*--clearbit=VAL*] [*--set=MASK*] [*--clear=MASK*] *name*
| **flux** **module** **trace** [-f] [*-t TYPE,...*] [-T *topic-glob*] [*name...*]



DESCRIPTION
===========

.. program:: flux module

:program:`flux module` manages dynamically loadable :man1:`flux-broker` modules.
Modules are automatically loaded at Flux instance start up (rc1) and unloaded
at Flux instance shutdown (rc3) using :man1:`flux-modprobe`, which is
configured to be aware of module dependencies.  :program:`flux module` is
more often used in test or debugging situations.

Broker modules are loaded under a *name* that is automatically registered
as a service endpoint. The name may be derived from the module file name
or overridden on the command line.  The same module may be loaded multiple
times under different names, although not all modules support this.  Once
loaded, the name is used as a handle for the loaded module instance by the
other sub-commands.

Broker modules communicate with other Flux components solely with messages
and utilize reactive (event loop) programming for concurrency, which presents
challenges for traditional debugging techniques.  Therefore
:program:`flux module` provides sub-commands for introspection and message
tracing.

Broker modules are often implemented as dynamic shared objects loaded
into broker threads, but they may also be loaded into separate processes.
This may be necessary for isolation, or to support incompatible programming
runtime environments.  A *module loader* process assists with loading modules
as separate processes, establishing communication with the broker, loading
and executing the module, and communicating with the broker's module
management framework.  The loader may be inferred from the module file name
suffix, or specified on the command line.  The following module loaders are
provided by flux-core:

module-exec (.so*)
  An alternate way to load traditional dynamic shared objects, with more
  isolation than direct broker loading.

A small number of modules that are integral to the broker are automatically
loaded early in broker start-up.  Although hard-wired to a particular name
and compiled into the broker executable, these modules otherwise behave
like regular broker modules and may be reloaded, traced, and introspected as
usual.

COMMANDS
========

load
----

.. program:: flux module load

Load *module*, which may be either the path to a shared object file, including
the ``.so`` suffix, or the basename of a shared object file on the
:envvar:`FLUX_MODULE_PATH`, without the suffix.

When :program:`flux module load` completes successfully, the new module has
entered the running state (see LIST OUTPUT below).

.. option:: -n, --name=NAME

  Override the default module name.

.. option:: --exec

  Force the module to be loaded as a separate process.

.. option:: --loader=PATH

  Specify a module loader path.  This option implies
  :option:`--exec` and only works when *module* is a path.

reload
------

.. program:: flux module reload

Reload module *name*. This is equivalent to running
:program:`flux module remove` followed by :program:`flux module load`.

.. option:: -f, --force

  Suppress failure if *module* is not loaded and proceed with loading.

.. option:: -n, --name=NAME

  Override the default module name.

.. option:: --exec

  Load the module in a separate process.

.. option:: --loader=PATH

  Specify a module loader path.  This option implies
  :option:`--exec` and only works when *module* is a path.


remove
------

.. program:: flux module reload

Remove module *name*.

.. option:: -f, --force

  Suppress failure if module *name* is not loaded.

.. option:: --cancel

  Call :linux:man3:`pthread_cancel` on a thread or send :const:`SIGKILL`
  to a process as appropriate to force a module to terminate.  This may be
  useful if the module is not able to respond to the module shutdown request
  because it has not returned control to its reactor loop.

  Note that broker modules running as threads are created with *deferred*
  cancellability.  This means that cancellation is only effective if the
  module thread calls one of the functions listed as a cancellation point
  in :linux:man7:`pthreads`.

list
----

.. program:: flux module list

List the loaded modules.

.. option:: -l, --long

  Include the full DSO path for each module.

stats
-----

.. program:: flux module stats

Request statistics from module *name*. A JSON object containing a set of
counters for each type of Flux message is returned by default, however
the object may be customized on a module basis.

.. option:: -p, --parse=OBJNAME

  *OBJNAME* is a period delimited list of field names that should be walked
  to obtain a specific value or object in the returned JSON.

.. option:: -t, --type=int|double

  Force the returned value to be converted to int or double.

.. option:: -s, --scale=N

  Multiply the returned (int or double) value by the specified
  floating point value.

.. option:: -R, --rusage=[self|children|thread]

  Return a JSON object representing an *rusage* structure
  returned by :linux:man2:`getrusage`.  If specified, the optional argument
  specifies the query target (default: self).

.. option:: -c, --clear

  Send a request message to clear statistics in the target module.

.. option:: -C, --clear-all

  Broadcast an event message to clear statistics in the target module
  on all ranks.

debug
-----

.. program:: flux module debug

Manipulate debug flags in module *name*. The interpretation of debug
flag bits is private to the module and its test drivers.

.. option:: -C, --clear

  Set all debug flags to 0.

.. option:: -S, --set=MASK

  Set debug flags to *MASK*.

.. option:: -s, --setbit=VAL

  Set one debug flag *VAL* to 1.

.. option:: -c, --clearbit=VAL

  Set one debug flag *VAL* to 0.

trace
-----

.. program:: flux module trace

Display message summaries for messages transmitted and received by the
named modules, or all modules if none are named.

.. note::

   Trace requests are accepted for modules before they are loaded.
   As a consequence, a trace request for a misspelled module name may be
   accepted, but produce no output.

.. option:: -f, --full

   Include JSON payload in output, if any.  Payloads that are not JSON are
   not displayed.

.. option:: -T, --topic=GLOB

   Filter output by topic string.

.. option:: -t, --type=TYPE,...

   Filter output by message type, a comma-separated list.  Valid types are
   ``request``, ``response``, ``event``, or ``control``.

.. option:: -L, --color[=WHEN]

  .. include:: common/color.rst

.. option:: -H, --human

   Display human-readable output. See also :option:`--color` and
   :option:`--delta`.

.. option:: -d, --delta

   With :option:`--human`, display the time delta between messages instead
   of a relative offset since the last absolute timestamp.


DEBUG OPTIONS
=============

.. program:: flux module debug

.. option:: -c, --clear

   Set debug flags to zero.

.. option:: -S, --set=MASK

   Set debug flags to MASK.
   The value may be prefixed with 0x to indicate hexadecimal or 0
   to indicate octal, otherwise the value is interpreted as decimal.

.. option:: -c, --clearbit=MASK

   Clear the debug bits specified in MASK without disturbing other bits.
   The value is interpreted as above.

.. option:: -s, --setbit=MASK

   Set the debug bits specified in MASK without disturbing other bits.
   The value is interpreted as above.


LIST OUTPUT
===========

.. program:: flux module list

The *list* command displays one line for each unique (as determined by
SHA1 hash) loaded module.

**Module**
   The value of the **mod_name** symbol for this module.

**Idle**
   Idle times are defined as the number of seconds since the module last sent
   a request or response message.

**State**
   The state of the module is shown as a single character: *I* initializing,
   *R* running, *F* finalizing, *E* exited.  A module automatically enters
   running state when it calls :man3:`flux_reactor_run`.  It can transition
   earlier by calling `flux_module_set_running()`.

**Service**
   If the module has registered additional services, the service names are
   displayed in a comma-separated list.

**Path**
   The full path to the broker module shared object file (only shown with
   the **-l, --long** option).


MODULE SYMBOLS
==============

All Flux modules define the following global symbols:

**const char \*mod_name;**
   A null-terminated string defining the module name.

**int mod_main (void \*context, int argc, char \**argv);**
   An entry function.


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_5`


SEE ALSO
========

:linux:man3:`syslog`
