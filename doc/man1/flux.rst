=======
flux(1)
=======


SYNOPSIS
========

**flux** [*OPTIONS*] *CMD* [*CMD-OPTIONS*]


DESCRIPTION
===========

Flux is a modular framework for resource management.

flux(1) is a front end for Flux sub-commands.
"flux -h" summarizes the core Flux commands.
"flux help *CMD*" displays the manual page for *CMD*.

If *CMD* contains a slash "/" character, it is executed directly,
bypassing the sub-command search path.


OPTIONS
=======

.. option:: -h, --help

   Display help on options, and a list of the core Flux sub-commands.

.. option:: -p, --parent

   If current instance is a child, connect to parent instead. Also sets
   *FLUX_KVS_NAMESPACE* if current instance is confined to a KVS namespace
   in the parent. This option may be specified multiple times.

.. option:: -v, --verbose

   Display command environment, and the path search for *CMD*.

.. option:: -V, --version

   Convenience option to run :man1:`flux-version`.


SUB-COMMAND ENVIRONMENT
=======================

flux(1) uses compiled-in install paths and its environment
to construct the environment for sub-commands.

Sub-command search path
   Look for "flux-*CMD*" executable by searching a path constructed
   with the following prototype:

   ::

      [getenv FLUX_EXEC_PATH_PREPEND]:install-path:\
        [getenv FLUX_EXEC_PATH]

setenv FLUX_MODULE_PATH
   Set up broker module search path according to:

   ::

      [getenv FLUX_MODULE_PATH_PREPEND]:install-path:\
        [getenv FLUX_MODULE_PATH]

setenv FLUX_CONNECTOR_PATH
   Set up search path for connector modules used by libflux to open a connection
   to the broker

   ::

      [getenv FLUX_CONNECTOR_PATH_PREPEND]:install-path:\
        [getenv FLUX_CONNECTOR_PATH]

setenv LUA_PATH
   Set Lua module search path:

   [getenv FLUX_LUA_PATH_PREPEND];[getenv LUA_PATH];install-path;

setenv LUA_CPATH
   Set Lua binary module search path:

   [getenv FLUX_LUA_CPATH_PREPEND];[getenv LUA_CPATH];install-path;

setenv PYTHONPATH
   Set Python module search path:

   ::

      [getenv FLUX_PYTHONPATH_PREPEND]:[getenv PYTHONPATH];install-path


RESOURCES
=========

Flux: http://flux-framework.org

