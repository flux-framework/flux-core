=====================
flux-shell-options(7)
=====================

.. program:: flux shell options

DESCRIPTION
===========

On startup, :program:`flux shell` examines the jobspec for shell-specific
options under the ``attributes.system.shell.options`` key. These options
control shell behavior and features including I/O redirection, CPU/GPU
affinity, signal handling, plugin configuration, and more.

Shell options may be set via :option:`flux submit -o, --setopt=OPT` option,
or explicitly added to the jobspec by other means.

Options may be simple boolean switches (e.g., ``verbose``) or may take
arguments. Since jobspec is a JSON document, shell options can accept
complex JSON objects as arguments, enabling flexible runtime configuration.

Option Format
-------------

Options specified without a value get the default value of 1:

.. code-block:: console

  $ flux run -o verbose myapp
  $ flux run -o pty myapp

To specify a boolean value use ``true`` or ``false`` explicitly:

.. code-block:: console

  $ flux run -o bool-option=true

Options with simple scalar arguments use ``=`` syntax:

.. code-block:: console

  $ flux run -o verbose=2 myapp
  $ flux run -o exit-timeout=5m myapp

Options with object arguments specify JSON:

.. code-block:: console

  $ flux run -o 'cpu-affinity={"verbose":true}' myapp

Note: Most options documented here use simplified syntax. See individual
option descriptions for object-based configuration when available.

Command-Line Convenience Options
---------------------------------

Many shell options have corresponding command-line options in the Flux
submission commands (:man1:`flux-run`, :man1:`flux-submit`,
:man1:`flux-batch`, :man1:`flux-alloc`) that provide convenient syntax
for common use cases. When available, these command-line options should
be preferred over setting shell options directly with ``-o``.

For example, use:

.. code-block:: console

  $ flux submit --signal=SIGUSR1@60s myapp

rather than:

.. code-block:: console

  $ flux submit -o signal.signum=10 -o signal.timeleft=60 myapp

Throughout this manual, cross-references to command-line options indicate
when a more convenient interface is available.

CORE OPTIONS
============

Shell Configuration
-------------------

.. option:: verbose[=INT]

  Set the shell verbosity to *INT*. Higher values increase verbosity:

  - 0: Errors only (default)
  - 1: Informational messages
  - 2: Debug messages including periodic resource monitoring

  .. code-block:: console

    $ flux run -o verbose=2 myapp

.. option:: initrc=FILE

  Load :program:`flux shell` initrc.lua file from *FILE* instead of the
  default system initrc path (``$sysconfdir/flux/shell/initrc.lua``).

  **Warning**: This completely replaces the system initrc, potentially
  bypassing default plugin loading. Use ``userrc`` instead to extend
  the system configuration.

  .. code-block:: console

    $ flux run -o initrc=/custom/initrc.lua myapp

.. option:: userrc=FILE

  Load an additional initrc.lua file after the system initrc. This is
  the recommended way to customize shell behavior without bypassing
  system defaults.

  For details of the initrc file format and available functions,
  see :man5:`flux-shell-initrc`.

  .. code-block:: console

    $ flux run -o userrc=$HOME/.flux/shell-initrc.lua myapp

PROCESS MANAGEMENT
==================

.. option:: nosetpgrp

  Disable the use of :linux:man2:`setpgrp` to launch each job task in its
  own process group.

  By default, the shell places each task in its own process group to ensure
  signals can be delivered independently. With ``nosetpgrp``, tasks remain
  in the shell's process group, meaning signals will only be delivered to
  direct children of the shell.

  This option is rarely needed but may be useful for debugging or when
  working with tools that expect a specific process group structure.

.. option:: stop-tasks-in-exec

  Stop tasks in ``exec()`` using ``PTRACE_TRACEME``. This causes each task
  to stop immediately before :linux:man2:`execve`, allowing a debugger to
  attach.

  Used internally by debugging tools. Users should not need to set this
  option directly.

.. option:: oom.adjust=VALUE

  Adjust each task's OOM (Out Of Memory) score to influence Linux's
  OOM killer behavior when system memory is critically low.

  - Value range: -1000 to 1000
  - 1000: Maximize probability of being killed (prefer killing this job)
  - 0: Default system behavior
  - -1000: Minimize probability of being killed (requires privilege)

  Setting negative values typically requires ``CAP_SYS_RESOURCE`` capability.

  For more information, refer to ``oom_score_adj`` in :linux:man5:`proc`.

  .. code-block:: console

    $ flux run -o oom.adjust=500 myapp

.. option:: rlimit

  A dictionary of soft process resource limits to apply before starting tasks.
  Resource limits are specified by lowercase name without the ``RLIMIT_``
  prefix, with integer values.

  Common limits include: ``core``, ``cpu``, ``data``, ``fsize``, ``nofile``,
  ``nproc``, ``stack``.

  See :linux:man2:`setrlimit` for available limits and :option:`flux-submit
  --rlimit` for command-line syntax.

EXIT BEHAVIOR
=============

.. option:: exit-timeout=VALUE

  When the first task in a job exits, a timer starts with duration
  specified by *VALUE* (in Flux Standard Duration format). If the timer
  expires before all tasks complete, a fatal exception is raised.

  - Default: ``30s`` for normal parallel jobs
    (:man1:`flux-submit`, :man1:`flux-run`)
  - Default: ``none`` for Flux instances 
    (:man1:`flux-batch`, :man1:`flux-alloc`)

  Valid *VALUE* formats:
  - FSD string: ``30s``, ``5m``, ``1h``
  - ``none``: Disable timeout completely

  This timeout helps detect hung jobs where some tasks fail to exit properly.

  .. code-block:: console

    $ flux run -o exit-timeout=5m myapp
    $ flux run -o exit-timeout=none myapp

.. option:: exit-on-error

  Raise a fatal job exception immediately if the first task to exit was
  signaled or exited with a nonzero status.

  Without this option, the shell waits for :option:`exit-timeout` to
  expire or all tasks to exit before exiting. With this option, the job
  fails fast on the first error.

  Useful for parallel workflows where one task failure invalidates the
  entire computation.

  .. code-block:: console

    $ flux run -o exit-on-error myapp

SIGNAL HANDLING
===============

.. option:: signal=OPTION

  Configure delivery of warning signal before job time limit expiration.

  This option is most easily set via :option:`flux-submit --signal`, which
  provides a convenient syntax for common use cases. :option:`--signal`
  option should be preferred over setting this shell option directly.

.. option:: signal.signum=NUMBER

  Signal number to send before time limit expiration. Must be used with
  ``signal.timeleft``.

.. option:: signal.timeleft=TIME

  Send ``signal.signum`` this amount of time before job expiration.
  TIME as an integer number of seconds or a string in Flux Standard Duration
  format.

CPU AFFINITY
============

.. option:: cpu-affinity=OPT

  Control CPU affinity binding for tasks. If unspecified, defaults to ``on``
  (bind each task to all allocated cores).

  *OPT* may be:

  **on**
    Bind each task to the full set of cores allocated to the job. This is
    the default and prevents tasks from being scheduled on cores outside
    the job allocation.

  **off**
    Disable CPU affinity completely. Tasks may float across all system cores.
    Useful when using external affinity tools like
    `mpibind <https://github.com/LLNL/mpibind>`_.

    .. code-block:: console

      $ flux run -o cpu-affinity=off myapp

  **per-task**
    Divide allocated cores evenly among local tasks. Each task is bound to
    its own subset of cores. If there are more tasks than cores, tasks share
    cores as evenly as possible.

    .. code-block:: console

      $ flux run -n 4 -o cpu-affinity=per-task myapp

    For a 4-task job on a node with 8 cores, each task receives 2 cores.

  **map:LIST**
    Explicitly specify CPU binding for each task using a semicolon-delimited
    list. Each entry can use :linux:man7:`hwloc` list, bitmask, or taskset
    format.

    See `hwlocality_bitmap(3)
    <https://www.open-mpi.org/projects/hwloc/doc/v2.9.0/a00181.php>`_
    for format details.

    .. code-block:: console

      $ flux run -n 3 -o 'cpu-affinity=map:0-1;2-3;4-5' myapp

    Task 0 binds to cores 0-1, task 1 to cores 2-3, task 2 to cores 4-5.

  **verbose**
    Log the cpuset assigned to the shell and each task. Must be combined
    with other options using comma separation, and must appear first.

    .. code-block:: console

      $ flux run -o cpu-affinity=verbose,per-task myapp

  **dry-run**
    Print cpusets without actually applying affinity bindings. Implies
    ``verbose``. Useful for testing affinity configurations. Must appear
    first when combined with other options.

    .. code-block:: console

      $ flux run -o cpu-affinity=dry-run,per-task myapp

GPU AFFINITY
============

.. option:: gpu-affinity=OPT

  Control GPU device visibility via ``CUDA_VISIBLE_DEVICES``. If unspecified,
  defaults to ``on`` (each task sees all GPUs allocated to the job).

  *OPT* may be:

  **on**
    Set ``CUDA_VISIBLE_DEVICES`` to include all GPUs allocated to the job.
    All tasks see the same GPU set.

    .. code-block:: console

      $ flux run -o gpu-affinity=on myapp

  **off**
    Disable the gpu-affinity plugin. ``CUDA_VISIBLE_DEVICES`` will not be
    set by the shell.

    .. code-block:: console

      $ flux run -o gpu-affinity=off myapp

  **per-task**
    Divide allocated GPUs evenly among local tasks. Each task's
    ``CUDA_VISIBLE_DEVICES`` includes only its assigned GPUs. If there are
    more tasks than GPUs, tasks share GPUs as evenly as possible.

    .. code-block:: console

      $ flux run -n 4 -o gpu-affinity=per-task myapp

  **map:LIST**
    Explicitly specify GPU assignment for each task using a semicolon-delimited
    list. Format is the same as ``cpu-affinity=map:LIST``.

    .. code-block:: console

      $ flux run -n 2 -o 'gpu-affinity=map:0;1' myapp

    Task 0 sees GPU 0, task 1 sees GPU 1.

INPUT/OUTPUT
============

.. option:: pty

  Allocate a pseudo-terminal (pty) to all task ranks. Output is captured
  to the same location as ``stdout``.

  Equivalent to: ``pty.ranks=all pty.capture``

  .. code-block:: console

    $ flux run -o pty myapp

.. option:: pty.ranks=OPT

  Specify which task ranks should have a pty allocated. *OPT* may be:

  - An RFC 22 IDset (e.g., ``0-3,5``)
  - A single integer rank
  - The string ``all`` for all ranks

  .. code-block:: console

    $ flux run -o pty.ranks=0-3 myapp

.. option:: pty.capture

  Capture pty output to the KVS alongside stdout. This is the default
  unless ``pty.interactive`` is set.

.. option:: pty.interactive

  Enable an interactive pty on rank 0, suitable for use with
  :man1:`flux job attach`.

  By default, only rank 0 gets a pty and output is not captured. Override
  these defaults with additional pty options:

  .. code-block:: console

    $ flux run -o pty.interactive -o pty.capture myapp

.. option:: output.{stdout,stderr}.type=TYPE

  Set output destination for stdout/stderr. *TYPE* may be:

  **kvs**
    Store output in the KVS (default). Retrieved via :man1:`flux job attach`.

  **term**
    Write directly to terminal (bypasses KVS).

  **file**
    Write to a file. Requires ``output.<stream>.path`` to be set.

  If only ``output.stdout.type`` is set, it applies to both streams.

  See also: :option:`flux-submit --output`, :option:`flux-submit --error`.

.. option:: output.{stdout,stderr}.path=PATH

  Set file path for stdout/stderr when ``output.<stream>.type=file``.

  Supports mustache templates for dynamic paths. See MUSTACHE TEMPLATES
  in :man1:`flux-submit` for full documentation.

.. option:: output.limit=SIZE

  Limit KVS output to *SIZE* bytes per stream. Once exceeded, output is
  truncated.

  - *SIZE* format: number with optional SI suffix (k, K, M, G)
  - Maximum: 1G
  - Default: 10M (multi-user instance), 1G (single-user instance)
  - Ignored for file output

  .. code-block:: console

    $ flux run -o output.limit=50M myapp

.. option:: output.mode=MODE

  Set file opening mode when writing output to files. *MODE* may be:

  **truncate**
    Overwrite existing files (default).

  **append**
    Append to existing files.

.. option:: output.{stdout,stderr}.buffer.type=[none|line]

  Set buffer type for stdout or stderr to line buffered or unbuffered (none).
  The default is line-buffered for stdout and unbuffered for stderr.
  See also the :option:`flux-submit --unbuffered` option.

.. option:: output.client.{lwm,hwm}=N

  Configure flow control for output aggregation on the leader shell.

  Flow control prevents unbounded memory growth when tasks produce output
  faster than it can be consumed. The shell uses a credit-based protocol:

  - Each shell starts with credits equal to ``hwm``
  - When credits drop to ``lwm``, request more from leader
  - At zero credits, output handling stops (tasks may block)
  - When credits arrive, output handling resumes

  Default values: ``lwm=100``, ``hwm=1000``

  These defaults are suitable for most cases. Adjust if experiencing output
  stalls or excessive memory use.

  .. code-block:: console

    $ flux run -o output.client.lwm=50 -o output.client.hwm=500 myapp

.. option:: input.stdin.type=TYPE

  Set input source for stdin. *TYPE* may be:

  **service**
    Read stdin from the shell's stdin service (default for interactive jobs).

  **file**
    Read stdin from a file.

  See also: :option:`flux-submit --input`.

  .. code-block:: console

    $ flux run --input=/tmp/input.data myapp

.. option:: output.batch-timeout=FSD

  Set the KVS output batch-timeout to a time period in Flux Standard Duration.
  This is the period over which the leader shell collects entries destined
  for the output eventlog before committing them to the KVS. A longer period
  results in less load on the KVS, while a shorter period makes output appear
  sooner after it was written by tasks in :command:`flux job attach`. The
  default is 0.5s.

  .. code-block:: console

   $ flux run -o output.batch-timeout=0.01 myapp

TASK MAPPING
============

.. option:: taskmap

  Request custom task-to-node mapping. This option is an object with
  required key ``scheme`` and optional key ``value``.

  The shell invokes ``taskmap.scheme`` plugin callbacks to generate the
  mapping. If ``value`` is set, it's passed to the plugin.

  Built-in schemes: ``block``, ``cyclic``, ``hostfile``, ``manual``

  See :option:`flux-submit --taskmap` for command-line syntax and
  :man7:`flux-shell-plugins` for implementing custom taskmap plugins.

  .. code-block:: console

    $ flux submit --taskmap=cyclic myapp

PMI CONFIGURATION
=================

.. option:: pmi=LIST

  Specify comma-separated list of PMI implementations to enable.
  Default: ``simple``

  Available implementations:

  **simple** (alias: **pmi1**, **pmi2**)
    The simple PMI-1 wire protocol. Passes an open file descriptor via
    :envvar:`PMI_FD`. Required for Flux's ``libpmi.so`` and ``libpmi2.so``.
    Preferred when Flux launches Flux (e.g., :man1:`flux-batch`).

  **off**
    Disable PMI completely.

  **cray-pals**
    External plugin from
    `flux-coral2 <https://github.com/flux-framework/flux-coral2>`_.

  **pmix**
    Provided via external plugin from the
    `flux-pmix <https://github.com/flux-framework/flux-pmix>`_. project

.. option:: pmi-simple.nomap

  Skip pre-populating ``flux.taskmap`` and ``PMI_process_mapping`` keys
  in the simple PMI implementation.

  Reduces PMI setup overhead when these keys are not needed.

.. option:: pmi-simple.exchange.k=N

  Configure PMI key exchange to use a virtual tree with fanout *N*.
  Default: 2

  Higher fanout reduces exchange time but increases message size and
  leader load.

STAGE-IN
========

The stage-in feature copies files from archived content into the job's
temporary directory before task execution. Files must be previously
archived using :man1:`flux-archive`.

.. option:: stage-in

  Enable stage-in. Copy files to the directory referenced by
  :envvar:`FLUX_JOB_TMPDIR` that were previously archived with
  :man1:`flux-archive`.

  .. code-block:: console

    $ flux run -o stage-in myapp

.. option:: stage-in.names=LIST

  Comma-separated list of archive names to extract. If no names are
  specified, ``main`` is assumed.

  .. code-block:: console

    $ flux run -o stage-in.names=main,data myapp

.. option:: stage-in.pattern=PATTERN

  Filter extracted files using a :man7:`glob` pattern.

  .. code-block:: console

    $ flux run -o 'stage-in.pattern=*.dat' myapp

.. option:: stage-in.destination=[SCOPE:]PATH

  Extract to *PATH* instead of :envvar:`FLUX_JOB_TMPDIR`.

  *SCOPE* may be:

  **local**
    Local file system (default). Extraction occurs on all nodes.

  **global**
    Global file system. Extraction occurs only on first node.

  .. warning::
    When using custom destinations, you must handle cleanup yourself.
    :envvar:`FLUX_JOB_TMPDIR` is automatically cleaned up and guaranteed
    to be unique.

  .. code-block:: console

    $ flux run -o 'stage-in.destination=global:/scratch/job-data' myapp

HWLOC CONFIGURATION
===================

.. option:: hwloc.xmlfile

  Export the job shell's hwloc XML topology to a file and set
  :envvar:`HWLOC_XMLFILE` for all tasks.

  This allows tasks to query the hardware topology without scanning the
  system directly. Also unsets :envvar:`HWLOC_COMPONENTS` which may
  interfere with ``HWLOC_XMLFILE``.

  .. code-block:: console

    $ flux run -o hwloc.xmlfile myapp

.. option:: hwloc.restrict

  Restrict the exported hwloc XML to only resources allocated to the job.

  Must be used with ``hwloc.xmlfile``. Without this option, the full node
  topology is exported.

  .. code-block:: console

    $ flux run -o hwloc.xmlfile -o hwloc.restrict myapp

MONITORING
==========

.. option:: sysmon

  Enable system resource monitoring. Logs peak memory usage and CPU load
  average for each shell at job completion.

  With ``verbose=2``, also traces memory and CPU usage periodically during
  execution.

  .. code-block:: console

    $ flux run -o sysmon myapp

.. option:: sysmon.period=FSD

  Set monitoring sample period in Flux Standard Duration format.
  Default: follows Flux heartbeat (typically 2 seconds)

  .. code-block:: console

    $ flux run -o sysmon -o sysmon.period=5s myapp

REXEC OPTIONS
=============

.. option:: rexec-shutdown-timeout=FSD

  Set timeout for processes launched via the rexec server to exit gracefully
  after all tasks complete.

  When all tasks exit, rexec processes receive SIGTERM. If they don't exit
  within this timeout, they receive SIGKILL.

  Default: 60s

  .. code-block:: console

    $ flux run -o rexec-shutdown-timeout=30s myapp

EXTERNAL PLUGIN OPTIONS
=======================

External job shell plugins may define additional options. Refer to
plugin-specific documentation for details. Plugin options typically use
a namespace prefix matching the plugin name (e.g., ``myplugin.enabled``).

See :man7:`flux-shell-plugins` for plugin development and configuration
details.

RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man1:`flux-shell`, :man5:`flux-shell-initrc`, :man7:`flux-shell-plugins`,
:man1:`flux-run`, :man1:`flux-submit`, :man1:`flux-batch`
