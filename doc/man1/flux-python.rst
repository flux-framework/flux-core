===============
flux-python(1)
===============


SYNOPSIS
========

**flux** **python** [*--get-path*] [*--list-versions*] [*ARGS...*]

**flux** **pythonX.Y** [*--get-path*] [*ARGS...*]


DESCRIPTION
===========

.. program:: flux python

:program:`flux python` provides convenient access to the Python interpreter
configured for the current Flux installation, with the Flux Python
bindings automatically available for import.

When invoked without options, :program:`flux python` prepends the Flux Python
bindings installation path to :envvar:`PYTHONPATH` and executes the configured
Python interpreter with all remaining arguments. This allows running Python
scripts or interactive sessions with immediate access to the correct **flux**
module for the current Flux installation without manual :envvar:`PYTHONPATH`
manipulation.

The command also supports special options to get the path to the bindings
as well as support discovery of available Python versions when bindings
for multiple Python version may be installed.

By default, Flux installs Python bindings to a standard Python installation
path (e.g., ``site-packages``). However, prepending this entire directory
to :envvar:`PYTHONPATH` would place all modules in that location ahead of
the user's existing Python path, potentially overriding other installed
packages. To avoid this issue, Flux creates per-version symbolic links
to only the Flux Python bindings in an isolated directory under the Flux
library path. This isolated path is what :program:`flux python` adds to
:envvar:`PYTHONPATH` and what :option:`--get-path` reports, ensuring users
can access Flux bindings without inadvertently affecting the import order
of other Python modules.


OPTIONS
=======

.. option:: --get-path

   Print the installation path of the Flux Python bindings for this Python
   version and exit. This reports the isolated path containing only Flux
   bindings, not the full Python site-packages directory, allowing the
   bindings to be added to :envvar:`PYTHONPATH` without affecting the import
   order of other Python modules.

.. option:: --list-versions

   List available python bindings versions by looking for
   :program:`flux-pythonX.Y` wrapper scripts in :envvar:`FLUX_EXEC_PATH`.
   Each line of output shows a version-specific command (e.g., ``python3.10``,
   ``python3.11``) that can be invoked via :program:`flux pythonX.Y**`. This
   option helps discover which Python versions have Flux bindings installed
   in multi-version environments.


VERSION-SPECIFIC COMMANDS
==========================

When multiple Flux installations are built against different Python versions,
each installs a :program:`flux pythonX.Y` command (e.g., :program:`flux
python3.10`, :program:`flux python3.11`) corresponding to its target Python
version. These commands provide explicit access to Flux bindings for specific
Python versions.

The :program:`flux pythonX.Y` commands accept the same :option:`--get-path`
option as :program:`flux python`, allowing users to query the bindings path
for any installed Python version:

.. code-block:: console

   $ flux python3.10 --get-path
   /usr/lib/flux/python3.10


To discover which version-specific commands are available, use:

.. code-block:: console

   $ flux python --list-versions
   python3.10
   python3.11
   python3.12


EXAMPLES
========

Run a Python script with Flux bindings available:

.. code-block:: console

   $ flux python myscript.py

Start an interactive Python session:

.. code-block:: console

   $ flux python
   >>> import flux
   >>> h = flux.Flux()

Get the path to default Flux Python bindings:

.. code-block:: console

   $ flux python --get-path
   /usr/lib/flux/python3.11

Use a specific Python version:

.. code-block:: console

   $ flux python3.10 -c "import sys; print(sys.version)"
   3.10.12 (main, Nov 20 2023, 15:14:05) [GCC 11.4.0]

Manually set :envvar:`PYTHONPATH` for external tools:

.. code-block:: console

   $ export PYTHONPATH=$(flux python --get-path):$PYTHONPATH
   $ python3 -c 'import flux; print(flux.Flux().attr_get("rank"))'
   0

ENVIRONMENT
===========

.. envvar:: PYTHONPATH

   The :program:`flux python` command prepends the Flux bindings installation
   path to this environment variable before executing Python. Any
   existing :envvar:`PYTHONPATH` entries are preserved.

.. envvar:: FLUX_EXEC_PATH

   Used by :option:`--list-versions` to discover installed
   :program:`flux-pythonX.Y` commands across all directories in the Flux
   command search path.


RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man1:`flux`, :linux:man1:`python`
