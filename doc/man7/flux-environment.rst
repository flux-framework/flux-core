===================
flux-environment(7)
===================


DESCRIPTION
===========

The following environment variables are set by Flux or influence Flux.

.. _job_environment:

JOB ENVIRONMENT
===============

The following are set in the environment of each task spawned by
:man1:`flux-shell` as part of a Flux job.

.. envvar:: FLUX_JOB_ID

   The current jobid in F58 form.  F58 is a compact, non-numeric representation
   of Flux's 64-bit integer jobid.  If the numeric form is required, use e.g.:

   .. code-block:: shell

      NUMERIC_JOB_ID=$(flux job id $FLUX_JOB_ID)

.. envvar:: FLUX_JOB_ID_PATH

   A list of Flux jobids in F58 form, beginning with the separator ``/``,
   that uniquely identifies the job's position in the Flux instance hierarchy.
   For example, a job submitted to a batch job on a Flux system instance might
   have a path of ``/ƒD2e73NP/ƒDCB6RV``.

.. envvar:: FLUX_ENCLOSING_ID

   The jobid of the enclosing Flux instance, if it has one. The enclosing
   Flux instance is the one that ran :envvar:`FLUX_JOB_ID`. Depending on
   how the enclosing Flux instance was started, it may or may not have
   a jobid. If it was not launched by Flux, :envvar:`FLUX_ENCLOSING_ID`
   is not set.

   Example 1: A batch job that runs one MPI job is submitted to a Flux system
   instance. In the environment of the MPI job, :envvar:`FLUX_ENCLOSING_ID`
   refers to the batch jobid in the system instance.

   Example 2: An MPI job is submitted directly to a Flux system
   instance. Since the Flux system instance was not launched by Flux,
   :envvar:`FLUX_ENCLOSING_ID` is not set in the environment of the MPI job.

.. envvar:: FLUX_JOB_SIZE

   The number of tasks in the current job.

.. envvar:: FLUX_JOB_NNODES

   The total number of nodes hosting tasks on behalf of the current job.

.. note::

   :envvar:`FLUX_JOB_NNODES` is more precisely defined as the total number of
   :man1:`flux-shell` processes running tasks on behalf of the current job.
   Normally one shell is started per broker, and one broker is started per
   node.  However, in rare test setups, a large Flux instance is mocked by
   running multiple brokers per node.  In that case, this variable may not
   represent the physical node count.

.. envvar:: FLUX_TASK_RANK

   The zero-origin, global rank for this task.  Tasks are assigned ranks using
   a "block" algorithm by default, although :option:`flux submit --taskmap` may
   select other mapping algorithms.

   Example: 8 tasks on 2 nodes with block and cyclic task mapping:

   .. list-table::

      * - Mapping
        - Node 0
        - Node 1

      * - block
        - 0, 1, 2, 3
	- 4, 5, 6, 7

      * - cyclic
        - 0, 2, 4, 6
	- 1, 3, 5, 7


.. envvar:: FLUX_TASK_LOCAL_ID

   The zero-origin, local (to the node) rank for this task.

   Example: 8 tasks on 2 nodes:

   .. list-table::

      * - Node 0
        - Node 1

      * - 0, 1, 2, 3
	- 0, 1, 2, 3

.. envvar:: FLUX_JOB_CC

   When :option:`flux submit --cc` or :option:`flux bulksubmit --cc` is used
   to submit a set of jobs, :envvar:`FLUX_JOB_CC` is set to the the integer
   id of the current job in the set.

.. envvar:: FLUX_JOB_TMPDIR

   The path of a per-job temporary directory that is created on each host node
   before any tasks are started, and cleaned up after all tasks have exited.
   All a job's tasks on a given node share the same directory.

.. envvar:: FLUX_KVS_NAMESPACE

   Each job is assigned a unique, job-owner-writable Flux KVS key space that
   is independent of the default (primary) one and persists as such while the
   job is in the RUNNING state.  This environment variable is interpreted by
   the Flux KVS API and therefore :man1:`flux-kvs` as a directive to treat
   all operations as rooted in that space.  The job exec service and the job
   shell record the job's input, output, and a log of events in this space.

   After the job completes, the job's namespace is added to the primary
   namespace and becomes part of the read-only job record.

.. envvar:: PMI_RANK
	    PMI_SIZE
	    PMI_FD
	    PMI_SPAWNED
	    FLUX_PMI_LIBRARY_PATH

   The ``pmi`` shell plugin sets these variables in the job environment to
   aid in the bootstrap of parallel programs.  They are not set when the simple
   PMI server is disabled, e.g.  with :option:`flux run -opmi=none`.

   The :envvar:`PMI_*` variables are standard for PMI-1 and are described
   in Flux RFC 13.

   :envvar:`FLUX_PMI_LIBRARY_PATH` is set to the full path of Flux's
   ``libpmi.so`` shared library, which is normally not installed to standard
   system paths.  This exists as an aid to the pre-v5 OpenMPI Flux MCA plugins
   so that an MPI program running under Flux knows where to :func:`dlopen`
   the library for bootstrap.

.. envvar:: CUDA_VISIBLE_DEVICES
            CUDA_DEVICE_ORDER

   The ``gpubind`` shell plugin sets these variables in the job environment
   to assign GPU devices to tasks.  They are not set when GPU affinity is
   disabled with :option:`flux run -ogpu-affinity=off`.

.. envvar:: FLUX_URI

   :envvar:`FLUX_URI` overrides the default, compiled-in broker socket path
   in the Flux API, and by extension all the Flux commands.  In the job
   environment, it points to the local broker responsible for the job.


.. _initial_program_environment:

INITIAL PROGRAM ENVIRONMENT
===========================

The :man1:`flux-alloc` interactive shell and the :man1:`flux-batch` batch
script are examples of Flux initial programs. Flux deliberately avoids
setting many environment variables for the initial program to ensure
consistency and predictability across all methods of launching a Flux
instance.

The initial program environment will have at least the following environment
variables set:

.. envvar:: FLUX_URI

   Allows Flux commands to connect to the enclosing instance (the instance
   that started the initial program)

.. envvar:: FLUX_ENCLOSING_ID

   Set to the jobid of the enclosing instance, if it was launched as a Flux
   job.

If needed, Flux commands can be used to retrieve information that would
typically be available from the environment in other workload managers. For
example:

.. code-block:: shell

   BATCH_NNODES=$(flux resource list -n -o {nnodes})
   BATCH_NCORES=$(flux resource list -n -o {ncores})
   BATCH_NGPUS=$(flux resource list -n -o {ngpus})
   BATCH_HOSTLIST=$(flux getattr hostlist)

The advantage of this approach is that it works consistently, regardless
of how the Flux instance was launched.

The following set of environment variables are actively unset by the broker
to avoid confusion when they are set by the enclosing instance or another
resource manager: :envvar:`FLUX_JOB_ID`, :envvar:`FLUX_JOB_SIZE`,
:envvar:`FLUX_JOB_NNODES`, :envvar:`FLUX_JOB_TMPDIR`,
:envvar:`FLUX_TASK_RANK`, :envvar:`FLUX_TASK_LOCAL_ID`,
:envvar:`FLUX_KVS_NAMESPACE`, :envvar:`FLUX_PROXY_REMOTE`,
:envvar:`FLUX_PMI_LIBRARY_PATH`, :envvar:`I_MPI_PMI_LIBRARY`,
:envvar:`PMI_*`, :envvar:`SLURM_*`

PMI CLIENT
==========

The :man1:`flux-broker` is capable of bootstrapping from configuration or
using a PMI client, similar to the way an MPI program bootstraps.  The broker's
PMI *client* is separate from the :man1:`flux-shell` PMI *server* offered to
parallel programs launched by Flux.  The following environment variables
affect the broker's PMI client.

.. envvar:: FLUX_PMI_DEBUG

   When set (to any value) in the broker's environment, PMI client tracing
   is enabled, causing PMI operations that occur during broker bootstrap to
   be logged to standard error.

.. envvar:: FLUX_PMI_CLIENT_METHODS

   Flux iterates through a list of PMI client implementations to find one that
   works.  By default the list is ``simple libpmi2 libpmi single``.  The
   sequence can be altered by setting this variable to a space-delimited list
   of client implementations.  The built-in ones are:

   simple
      Use the PMI-1 simple wire protocol.

   libpmi2[:PATH]
      :func:`dlopen` ``libpmi2.so`` and use the PMI-2 API, optionally at
      a specific *PATH*.

   libpmi[:PATH]
      :func:`dlopen` ``libpmi.so`` and use the PMI-1 API, optionally at
      a specific *PATH*.

   single
      Become a singleton.  This always succeeds so should be the last method.

.. envvar:: FLUX_PMI_CLIENT_SEARCHPATH

   A colon-separated list of directories to search for PMI client plugins.
   Client plugins can be packaged separately from flux-core.

.. envvar:: FLUX_IPADDR_HOSTNAME

   When bootstrapping with PMI, the broker dynamically selects an TCP address
   to bind to for overlay network communication, which it then exchanges with
   peers using PMI.  By default, it tries to use the address associated with
   the default route.  Setting this variable to any value in the broker's
   environment directs it to prefer the address associated with the system
   :linux:man1:`hostname` instead.

.. envvar:: FLUX_IPADDR_V6

   When dynamically selecting an address to use with PMI, the broker prefers IP
   version 4 addresses.  Setting this variable to any value in the broker's
   environment causes it to prefer version 6 addresses.

.. envvar:: FLUX_IPADDR_INTERFACE

   Force PMI bootstrap to assign the broker an address associated with a
   particular network interface, like ``eth0``.  Alternatively, the interface
   may be specified by its IPv4 network address in CIDR form, e.g.
   "192.168.1.1/24".


CUSTOM OUTPUT FORMATS
=====================

Sites and individual users may create custom output formats for some Flux
commands.  The formats are expressed in configuration files with a base name
of the command name plus a ``.toml``, ``.yaml``, or ``.json`` extension,
stored in directories that follow the `XDG Base Directory Specification
<https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html>`_.

Named formats are merged and/or overridden in the following order:

   #. internal defaults
   #. config files found in a ``flux`` sub-directory of the
      :envvar:`XDG_CONFIG_DIRS` directories
   #. config files found in a ``flux`` sub-directory of
      :envvar:`XDG_CONFIG_HOME`

For more information on named formats see the individual command documentation
and the :ref:`flux_jobs_configuration` section of :man1:`flux-jobs`.

.. envvar:: XDG_CONFIG_DIRS

   A colon-separated, preference-ordered list of base directories to search for
   configuration files in addition to the :envvar:`XDG_CONFIG_HOME` base
   directory.  If unset, ``/etc/xdg`` is used.

.. envvar:: XDG_CONFIG_HOME

   The base directory for user-specific configuration files.  If unset,
   ``$HOME/.config`` is used.

.. envvar:: FLUX_JOBS_FORMAT_DEFAULT
            FLUX_RESOURCE_STATUS_FORMAT
            FLUX_RESOURCE_LIST_FORMAT_DEFAULT
            FLUX_QUEUE_LIST_FORMAT_DEFAULT
            FLUX_PGREP_FORMAT_DEFAULT

   In addition to registering custom named formats, users and sites can change
   the default output format to one of the named formats by setting an
   environment variable to the format name.  The above variables affect the
   default output of :man1:`flux-jobs`, :man1:`flux-resource`,
   :man1:`flux-queue`, and :man1:`flux-pgrep`.


SYSTEM SCRIPTS
==============

The Flux :term:`system instance` may configure prolog, epilog, and/or
housekeeping scripts to run as root before or after each job.  The script
environment is restricted by the IMP for security reasons, but it always
contains the following:

- :envvar:`PATH` - a safe value for root such as
  ``/usr/sbin:/usr/bin:/sbin:/bin``
- :envvar:`USER` - ``root``
- :envvar:`HOME` - root's home directory
- :envvar:`FLUX_OWNER_USERID` - the uid of the Flux system
  :term:`instance owner`
- :envvar:`FLUX_JOB_ID` - the job id that triggered the script
- :envvar:`FLUX_JOB_USERID` - the uid of the of the job owner
- :envvar:`FLUX_JOB_RANKS` - the broker ranks which were assigned to the
  job for which the current script is running.  This can be used in conjunction
  with the ``hostlist`` attribute to construct the job hostlist. For example:

  .. code-block:: shell

   FLUX_JOB_HOSTLIST=$(flux hostlist --nth=${FLUX_JOB_RANKS} instance)

If the IMP is configured to allow other ``FLUX_`` prefixed environment
variables to be set as described in :man5:`flux-config-security-imp`,
then the following are set to allow Flux commands to work from the script:

- :envvar:`FLUX_URI`
- :envvar:`FLUX_MODULE_PATH`
- :envvar:`FLUX_EXEC_PATH`
- :envvar:`FLUX_CONNECTOR_PATH`

TESTING
=======

The following environment variables are primarily useful when debugging Flux
components or writing tests.

.. envvar:: FLUX_HANDLE_TRACE

   If set in the environment of a Flux component, the ``FLUX_O_TRACE`` flag
   is automatically set in any call to :man3:`flux_open`.  This causes decoded
   messages passed over the :c:type:`flux_t` handle to be decoded and printed
   on standard error.

.. envvar:: FLUX_HANDLE_MATCHDEBUG

   If set in the environment of a Flux component, the ``FLUX_O_MATCHDEBUG``
   flag is automatically set in any call to :man3:`flux_open`.  This causes a
   diagnostic to be printed to standard error if any matchtags are leaked when
   the broker connection is closed.

.. envvar:: FLUX_HANDLE_USERID

   Mock a user.  If set to a numerical user ID in the environment of a Flux
   component, all messages sent by the component appear to have been sent by
   this user.  This is useful for testing code that authorizes actions based on
   the identity of the requesting user.  This is restricted to the instance
   owner.

.. envvar:: FLUX_HANDLE_ROLEMASK

   Mock a rolemask (capability set).  If set to a decimal or hex (``0x``
   prefixed) value in the environment of a Flux component, all messages sent
   by the component are stamped with this rolemask. This is useful for testing
   code that authorizes actions based on the possession of particular roles.
   This is restricted to the instance owner.

.. envvar:: FLUX_HWLOC_XMLFILE

   Flux discovers available resources dynamically using `HWLOC
   <https://www.open-mpi.org/projects/hwloc/>`_.  In some cases dynamic
   discovery is not desired, such as when it causes poor performance in
   parallel testing.  Flux may be directed to read topology from an XML file
   instead by setting :envvar:`FLUX_HWLOC_XMLFILE` to the file path.

   :program:`flux resource reload` offers a related mechanism for loading a
   set of HWLOC xml files directly into the instance resource inventory
   for test scenarios.

.. envvar:: FLUX_URI_RESOLVE_LOCAL

   If set, force :man1:`flux-uri` and the URI resolver embedded in other
   commands to resolve URIs to local form.  This is useful in test environments
   where the remote connector does not work.

.. envvar:: FLUX_RESOURCE_LIST_RPC

   If set, :man1:`flux-resource` uses the specified RPC topic string instead
   of ``resource.sched-status``.  This is used in test to verify that the
   ``sched.resource-status`` RPC used in earlier releases still works for
   backwards compatibility.

.. envvar:: FLUX_LOAD_WITH_DEEPBIND

   By default flux loads all modules, plugins and dlopened libraries of any kind
   with RTLD_DEEPBIND to avoid symbol conflicts.  If this environment variable
   is set to 0 that flag will be cleared from the flags of all dlopen
   invocations. This is mainly useful to override the allocator or otherwise
   interpose a tool or library with LD_PRELOAD.  Be aware that this can cause
   symbol conflicts with plugins, and is not recommended for production.

.. envvar:: FLUX_HOSTLIST_STDIN_TIMEOUT

   The :command:`flux-hostlist` command reads from stdin by default. If no
   data is available within 15s, the command times out to prevent a permanent
   hang. This environment variable can be used to modify or disable (set to 0)
   the timeout.

.. envvar:: FLUX_PYCLI_LOGLEVEL

   Controls the logging level for Flux Python command line tools. By
   default, tools use ``logging.INFO``, which suppresses debug
   messages and exception backtraces. Set this variable to a
   numeric logging level to override the default. See the Python
   `logging <https://docs.python.org/3/library/logging.html#levels>`_
   documentation for valid numeric values. The most useful setting is
   :envvar:`FLUX_PYCLI_LOGLEVEL=10`, which sets ``logging.DEBUG`` with
   debug messages and full backtraces.

MISCELLANEOUS
=============

.. envvar:: FLUX_F58_FORCE_ASCII

   A locale or terminal misconfiguration can cause the ``ƒ`` character used in
   Flux jobids to be rendered incorrectly.  As a workaround, set this variable
   and ASCII ``f`` is used instead.

.. envvar:: FLUX_CONF_DIR

   If set in in the :man1:`flux-broker` environment, configuration files
   matching ``*.toml`` are loaded from the specified directory.  The
   :option:`flux broker --config-path` option does that too, and is more
   flexible in that it can also load single files in TOML or JSON format.

.. envvar:: FLUX_ATTACH_NONINTERACTIVE

   If set, never show the status line in :program:`flux job attach` output.

.. envvar:: FLUX_PROXY_REMOTE

   When :man1:`flux-proxy` connects to a remote instance, it sets this variable
   to the authority part of the remote URI.  This serves as a hint to
   :man3:`flux_attr_get` to transform the value of the ``parent-uri`` broker
   attribute into a remote URI so it can work from the remote proxy environment.
   For example:

   .. code-block:: shell

      $ flux alloc -N1
      f(s=1,d=1) $ flux getattr parent-uri
      local:///run/flux/local

   .. code-block:: shell

      $ flux proxy $(flux job last)
      ƒ(s=1,d=1) $ printenv FLUX_PROXY_REMOTE
      test0
      ƒ(s=1,d=1) $ flux getattr parent-uri
      ssh://test0/run/flux/local

.. envvar:: FLUX_TERMINUS_SESSION

   The current terminus session ID.  A terminus session is started when the
   job has an interactive pseudo-terminal, which occurs when a job is run with
   :option:`flux run -o pty.interactive`, or when a Flux instance is started
   with :man1:`flux-alloc`.

.. envvar:: FLUX_MODPROBE_PATH

   Override the default search path in :man1:`flux-modprobe`. Normally,
   after loading the built-in default ``modprobe.toml`` and ``rcX.py``,
   :command:`flux modprobe` loads all extra module configuration from
   ``modprobe.d/*.toml`` using the search path::

     $datadir/flux/modprobe:$sysconfdir/flux/modprobe

   and all rc Python files from ``rcX.d/*.py`` using the search path::

     $libexecdir/flux/modprobe:$sysconfdir/flux/modprobe

   :envvar:`FLUX_MODPROBE_PATH` overrides these internal paths. If set to
   an empty string, then the internal default search paths are disabled.

.. envvar:: FLUX_MODPROBE_PATH_APPEND

   If set to a colon-separated list of directories, :man1:`flux-modprobe`
   will append these directories to its default search path instead of
   overriding as with :envvar:`FLUX_MODPROBE_PATH`.

.. envvar:: FLUX_RC_EXTRA

   If set to a colon-separated list of directories, the installed
   :man1:`flux-broker` rc scripts search these directories for additional
   scripts to run during broker initialization and finalization.

   Specifically the ``rc1`` script runs ``rc1.d/*`` in each directory
   and the ``rc3`` script runs ``rc3.d/*`` in each directory.

.. envvar:: FLUX_SHELL_RC_PATH

   Set to a colon-separated list of directories to be added to the directories
   that :man1:`flux-shell` searches for lua scripts to extend its initrc.

.. envvar:: FLUX_SSH

   Override the compiled-in path to the :program:`ssh` executable used by the
   ssh connector.  The ssh connector is invoked when attempting to open a
   connection to Flux with a URI that begins with ``ssh://``.

.. envvar:: FLUX_SSH_RCMD

   Override the heuristically-determined remote path to the :man1:`flux`
   command front end executable used by the ssh connector to start
   :program:`flux relay` on the remote system.

.. envvar:: DBUS_SESSION_BUS_ADDRESS

   :man1:`flux-exec` sets this to point to the Flux instance owner's
   D-Bus instance, to ensure that a remote invocation of
   :option:`systemctl --user` accesses the service manager for the Flux
   instance owner.  This is helpful when debugging a system instance
   configured to launch jobs with systemd, as described in
   :man5:`flux-config-exec`.


.. _sub_command_environment:

SUB-COMMAND ENVIRONMENT
=======================

:man1:`flux` sets up the environment for sub-commands using a combination
of compiled-in install paths and the environment.

.. note::
   The PREPEND versions of environment variables below may
   be necessary when developing and testing a new version
   of a Flux command (:envvar:`FLUX_EXEC_PATH_PREPEND`),
   module (:envvar:`FLUX_MODULE_PATH_PREPEND`), connector
   (:envvar:`FLUX_CONNECTOR_PATH_PREPEND`), or Python module
   (:envvar:`FLUX_PYTHONPATH_PREPEND`) when an existing version of that
   component is already installed in the system default paths. Otherwise,
   the installed component would always be used by the system Flux, since
   the installed paths are always placed first in the subcommand environment
   created by :man1:`flux`.

.. envvar:: FLUX_EXEC_PATH
            FLUX_EXEC_PATH_PREPEND

   :man1:`flux` finds sub-command executables by searching:

      $FLUX_EXEC_PATH_PREPEND : install-path : $FLUX_EXEC_PATH

   Values may include multiple directories separated by colons.

.. envvar:: FLUX_MODULE_PATH
            FLUX_MODULE_PATH_PREPEND

   :envvar:`FLUX_MODULE_PATH` is set in the environment of the broker
   so that broker modules can be found and loaded when requested by
   :man1:`flux-module`:

      $FLUX_MODULE_PATH_PREPEND : install-path : $FLUX_MODULE_PATH

   Values may include multiple directories separated by colons.

.. envvar:: FLUX_CONNECTOR_PATH
            FLUX_CONNECTOR_PATH_PREPEND

   :envvar:`FLUX_CONNECTOR_PATH` is set in the environment of sub-commands so
   that :man3:`flux_open` can find the connector corresponding to the URI
   scheme:

      $FLUX_CONNECTOR_PATH_PREPEND : install-path : $FLUX_CONNECTOR_PATH

   Values may include multiple directories separated by colons.

.. envvar:: PYTHONPATH
            FLUX_PYTHONPATH_PREPEND

   With the :command:`flux python` and :man1:`flux-env` subcommands,
   :envvar:`PYTHONPATH` is set such that the correct version of
   Python modules can be found. In these cases the path is set
   to the following:

      $FLUX_PYTHONPATH_PREPEND : install-path : $PYTHONPATH

   The :man1:`flux` command does not otherwise modify :envvar:`PYTHONPATH`
   unless :envvar:`FLUX_PYTHONPATH_PREPEND` is set.

   Values may include multiple directories separated by colons.

.. note::
   Flux commands written in Python further modify Python's
   `sys.path <https://docs.python.org/3/library/sys.html#sys.path>`_
   to ensure that interpreter default paths appear before any custom values
   set in :envvar:`PYTHONPATH`. This is an attempt to  avoid incompatible
   modules interfering with the operation of Flux commands. If it becomes
   necessary to force a non-standard module first in the search path (e.g.
   for testing, instrumentation, etc.) then :envvar:`FLUX_PYTHONPATH_PREPEND`
   should be used.

.. envvar:: LUA_PATH
            LUA_CPATH
            FLUX_LUA_PATH_PREPEND
            FLUX_LUA_CPATH_PREPEND

   :envvar:`LUA_PATH` and :envvar:`LUA_CPATH` are set so that sub-commands can
   find required Lua libraries.  They are set, respectively, to

      $FLUX_LUA_PATH_PREPEND ; install-path ; $LUA_PATH ;;

      $FLUX_LUA_CPATH_PREPEND ; install-path ; $LUA_CPATH ;;

   Values may include multiple directories separated by semicolons.

RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man1:`flux-env`
