==============
flux-module(1)
==============


SYNOPSIS
========

**flux** **module** *COMMAND* [*OPTIONS*]


DESCRIPTION
===========

flux-module(1) manages dynamically loadable :man1:`flux-broker` modules.


COMMANDS
========

**load** *name* [*module-arguments* …​]
   Load module *name*.  When the load command completes successfully,
   the new module has entered the running state (see LIST OUTPUT below).

**remove** [--force] *name*
   Remove module *name*.  If *-f, --force* is used, then do not fail if
   module *name* is not loaded.

**reload** [--force] *name* [*module-arguments* …​]
   Reload module *name*. This is equivalent to running *flux module remove*
   followed by *flux module load*. It is a fatal error if module *name* is
   not loaded during removal unless the ``-f, --force`` option is specified.

**list** [*service*]
   List loaded :man1:`flux-broker` modules.

**stats** [*OPTIONS*] [*name*]
   Request statistics from module *name*. A JSON object containing a set of
   counters for each type of Flux message is returned by default, however
   the object may be customized on a module basis.

**debug** [*OPTIONS*] [*name*]
   Manipulate debug flags in module *name*. The interpretation of debug
   flag bits is private to the module and its test drivers.


STATS OPTIONS
=============

.. option:: -p, --parse=OBJNAME

   OBJNAME is a period delimited list of field names that should be walked
   to obtain a specific value or object in the returned JSON.

.. option:: -t, --type=int|double

   Force the returned value to be converted to int or double.

.. option:: -s, --scale=N

   Multiply the returned (int or double) value by the specified
   floating point value.

.. option:: -R, --rusage

   Return a JSON object representing an *rusage* structure
   returned by :linux:man2:`getrusage`.

.. option:: -c, --clear

   Send a request message to clear statistics in the target module.

.. option:: -C, --clear-all

   Broadcast an event message to clear statistics in the target module
   on all ranks.


DEBUG OPTIONS
=============

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

Flux: http://flux-framework.org


SEE ALSO
========

:linux:man3:`syslog`
