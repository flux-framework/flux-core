========================
flux_conf_builtin_get(3)
========================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  const char *flux_conf_builtin_get (const char *key,
                                     enum flux_conf_flags hint);


Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_conf_builtin_get` retrieves compiled-in flux-core
parameters by :var:`key`.  The return value depends on a context
:var:`hint`:

FLUX_CONF_INSTALLED
  Return paths relative to install directories.

FLUX_CONF_INTREE
  Return paths relative to the source tree directory.

FLUX_CONF_AUTO
  Try to determine the context automatically.

KEYS
====

The following keys are valid:

confdir
  The default directory where Flux configuration files are stored.

libexecdir
  The directory where flux-core internal binaries that are not intended
  to be executed directly by users or shell scripts are installed.

datadir
  The directory where flux-core architecture independent data files
  are installed.

lua_cpath_add, lua_path_add
  The directory where flux-core LUA modules are installed.

python_path
  The directory where flux-core python site-packages are installed.

python_wrapper
  The path to a wrapper script that executes flux subcommands with
  :envvar:`PYTHONPATH` set appropriately.

man_path
  The directory where flux man pages are installed.

exec_path
  The directory where flux-core subcommand executables are installed.

connector_path
  The directory where flux-core connector plugins are installed.

module_path
  The directory where flux-core broker modules are installed.

cmdhelp_pattern
  A glob matching data files used to generate :man1:`flux` help output.

pmi_library_path
  The installed path of flux's PMI-1 shared library.

shell_path
  The installed path of the :man1:`flux-shell` executable.

shell_pluginpath
  The directory where flux-core shell plugin modules are installed.

shell_initrc
  The installed path of the default :man1:`flux-shell` initrc file.

jobtap_pluginpath
  The directory where flux-core jobtap plugin modules are installed.

upmi_pluginpath
  The directory where flux-core PMI client plugins are installed.

no_docs_path
  The path where a sentinel file is installed if flux-core was built
  without man pages.

rundir
  The default directory for run-time variable data.

RETURN VALUE
============

:func:`flux_conf_builtin_get` returns a value string, or NULL on error.
:var:`errno` is not set.

ERRORS
======

ENOMEM
   Out of memory.

RESOURCES
=========

.. include:: common/resources.rst

Filesystem Hierarchy Standard https://refspecs.linuxfoundation.org/fhs.shtml

SEE ALSO
========

:man1:`flux-config`
