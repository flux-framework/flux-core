=======
flux(1)
=======


SYNOPSIS
========

**flux** [*OPTIONS*] *CMD* [*CMD-OPTIONS*]


DESCRIPTION
===========

.. program:: flux

Flux is a modular framework for resource management.

:program:`flux` is a front end for Flux sub-commands.
:option:`flux -h` summarizes the core Flux commands.
:program:`flux help CMD` displays the manual page for *CMD*.

If *CMD* contains a slash "/" character, it is executed directly,
bypassing the sub-command search path.


OPTIONS
=======

.. option:: -h, --help

   Display help on options, and a list of the core Flux sub-commands.

.. option:: -p, --parent

   If current instance is a child, connect to parent instead. Also sets
   :envvar:`FLUX_KVS_NAMESPACE` if current instance is confined to a KVS
   namespace in the parent. This option may be specified multiple times.

.. option:: -r, --root

   Like :option:`--parent`, but connect to the top-level, or root, instance.
   This overrides any other uses of the :option:`--parent` option.

.. option:: -v, --verbose

   Display command environment, and the path search for *CMD*.

.. option:: -V, --version

   Convenience option to run :man1:`flux-version`.


SUB-COMMAND ENVIRONMENT
=======================

:program:`flux` uses compiled-in install paths and its environment
to construct the environment for sub-commands.  More detail is available in the
:man7:`flux-environment` :ref:`sub_command_environment` section.  A summary
is provided below:

.. list-table::
   :header-rows: 1

   * - Name
     - Description

   * - :envvar:`FLUX_EXEC_PATH`
     - where to look for "flux-*CMD*" executables

   * - :envvar:`FLUX_MODULE_PATH`
     - directories to look for broker modules

   * - :envvar:`FLUX_CONNECTOR_PATH`
     - directories to search for connector modules

   * - :envvar:`LUA_PATH`
     - Lua module search path

   * - :envvar:`LUA_CPATH`
     - Lua binary module search path

   * - :envvar:`PYTHONPATH`
     - Python module search path:


RESOURCES
=========

.. include:: common/resources.rst

