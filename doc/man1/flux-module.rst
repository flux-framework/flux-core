.. flux-help-include: true

==============
flux-module(1)
==============


SYNOPSIS
========

**flux** **module** *COMMAND* [*OPTIONS*]


DESCRIPTION
===========

flux-module(1) manages dynamically loadable Flux modules.
It can load/remove/list modules for the flux-broker(1), and for other
Flux services that support dynamic module extensions.


COMMANDS
========

**info** [*name*]
   Display information about module *name*.
   If *name* includes a slash */* character, it is interpreted as a
   file path, and the module name is then determined by reading the
   **mod_name** symbol. Otherwise, FLUX_MODULE_PATH is searched for a module
   with **mod_name** equal to *name*.

**load** *name* [*module-arguments* …​]
   Load module *name*, interpreted as described above.
   The service that will load the module is inferred
   from the module name. When the load command completes successfully,
   the new module is ready to accept messages on all targeted ranks.

**remove** [--force] *name*
   Remove module *name*. The service that will unload the module is
   inferred from the name specified on the command line. If *-f, --force*
   is used, then do not error if module *name* is not loaded.

**reload** [--force] *name* [*module-arguments* …​]
   Reload module *name*. This is equivalent to running *flux module remove*
   followed by *flux module load*. It is a fatal error if module *name* is
   not loaded during removal unless the ``-f, --force`` option is specified.

**list** [*service*]
   List modules loaded by *service*, or by flux-broker(1) if *service* is unspecified.

**stats** [*OPTIONS*] [*name*]
   Request statistics from module *name*. A JSON object containing a set of
   counters for each type of Flux message is returned by default, however
   the object may be customized on a module basis.

**debug** [*OPTIONS*] [*name*]
   Manipulate debug flags in module *name*. The interpretation of debug
   flag bits is private to the module and its test drivers.


STATS OPTIONS
=============

**-p, --parse**\ *=OBJNAME*
   OBJNAME is a period delimited list of field names that should be walked
   to obtain a specific value or object in the returned JSON.

**-t, --type**\ *=int|double*
   Force the returned value to be converted to int or double.

**-s, --scale**\ *=N*
   Multiply the returned (int or double) value by the specified
   floating point value.

**-R, --rusage**
   Return a JSON object representing an *rusage* structure
   returned by getrusage(2).

**-c, --clear**
   Send a request message to clear statistics in the target module.

**-C, --clear-all**
   Broadcast an event message to clear statistics in the target module
   on all ranks.


DEBUG OPTIONS
=============

**-c, --clear**
   Set debug flags to zero.

**-S, --set**\ *=MASK*
   Set debug flags to MASK.
   The value may be prefixed with 0x to indicate hexadecimal or 0
   to indicate octal, otherwise the value is interpreted as decimal.

**-c, --clearbit**\ *=MASK*
   Clear the debug bits specified in MASK without disturbing other bits.
   The value is interpreted as above.

**-s, --setbit**\ *=MASK*
   Set the debug bits specified in MASK without disturbing other bits.
   The value is interpreted as above.


LIST OUTPUT
===========

The *list* command displays one line for each unique (as determined by
SHA1 hash) module loaded by a service.

**Module**
   The value of the **mod_name** symbol for this module.

**Size**
   The size in bytes of the module .so file.

**Digest**
   The last 7 characters of the SHA1 digest of the contents of
   the module .so file.

**Idle**
   Idle times are defined for flux-broker(1) comms modules as the number of
   heartbeats since the module last sent a request or response message.
   The idle time may be defined differently for other services, or have no
   meaning.


MODULE SYMBOLS
==============

All Flux modules define the following global symbols:

**const char \*mod_name;**
   A null-terminated string defining the module name.
   Module names are words delimited by periods, with the service that
   will load the module indicated by the words that prefix the final one.
   If there is no prefix, the module is loaded by flux-broker(1).

**int mod_main (void \*context, int argc, char \**argv);**
   An entry function.


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

syslog(3)
