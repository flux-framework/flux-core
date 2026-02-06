=====================
flux-shell-options(7)
=====================

.. program:: flux shell options

DESCRIPTION
===========

On startup, :program:`flux shell` will examine the jobspec for any shell
specific options under the ``attributes.system.shell.options`` key.  These
options may be set by the :option:`flux submit -o, --setopt=OPT` option,
or explicitly added to the jobspec by other means.

Job shell options may be switches to enable or disable a shell feature or
plugin, or they may take an argument. Because jobspec is a JSON document,
job shell options in jobspec may take arguments that are themselves
JSON objects. This allows maximum flexibility in runtime configuration
of optional job shell behavior. In the list below, if an option doesn't
include a ``=``, then it is a simple boolean option or switch and may be
specified simply with :option:`flux submit -o OPTION`.

Job shell plugins may also support configuration via shell options in
the jobspec. For specific information about runtime-loaded plugins,
see the documentation for the specific plugin in question.

Shell options supported by :program:`flux shell` itself and its built-in
plugins include:

.. option:: verbose[=INT]

  Set the shell verbosity to *INT*. A larger value indicates increased
  verbosity, though setting this value larger than 2 currently has no
  effect.

.. option:: nosetpgrp

  Disable the use of :linux:man2:`setpgrp` to launch each
  job task in its own process group. This will cause signals to be
  delivered only to direct children of the shell.

.. option:: initrc=FILE

  Load :program:`flux shell` initrc.lua file from *FILE* instead of the default
  initrc path. For details of the job shell initrc.lua file format,
  see :man5:`flux-shell-initrc`.

.. option:: userrc=FILE

  Load another initrc.lua file after the system one.  For details of the
  job shell initrc.lua file format, see :man5:`flux-shell-initrc`.

.. option:: pty

  Allocate a pty to all task ranks for non-interactive use. Output
  from all ranks will be captured to the same location as ``stdout``.
  This is the same as setting :option:`pty.ranks=all` and :option:`pty.capture`.
  (see below).

.. option:: pty.ranks=OPT

  Set the task ranks for which to allocate a pty. *OPT* may be either
  an RFC 22 IDset of target ranks, an integer rank, or the string "all"
  to indicate all ranks.

.. option:: pty.capture

  Enable capture of pty output to the same location as stdout. This is
  the default unless :option:`pty.interactive` is set.

.. option:: pty.interactive

  Enable a pty on rank 0 that is set up for interactive attach by
  a front-end program (i.e. :program:`flux job attach`). With no other
  :option:`pty` options, only rank 0 will be assigned a pty and output will not
  be captured. These defaults can be changed by setting other
  :option:`pty` options after :option:`pty.interactive`, e.g.

  .. code-block:: console

    $  flux run -o pty.interactive -o pty.capture ...

  would allocate an interactive pty on rank 0 and also capture the
  pty session to the KVS (so it can be displayed after the job exits
  with ``flux job attach``).

.. option:: cpu-affinity=OPT

  Adjust the operation of the builtin shell ``affinity`` plugin.  If the
  option is unspecified, ``on`` is assumed.  *OPT* may be set to:

  on
    Bind each task to the full set of cores allocated to the job.

  off
    Disable the affinity plugin.  This may be useful if using another plugin
    such as `mpibind <https://github.com/LLNL/mpibind>`_ to manage CPU
    affinity.

  per-task
    Bind each task to an evenly populated subset of the cores allocated to
    the job.  Tasks share cores only if there are more tasks than cores.

  map:LIST
    Bind each task to *LIST*, a semicolon-delimited list of cores.
    Each entry in the list can be in one of the :linux:man7:`hwloc`
    *list*, *bitmask*, or *taskset* formats. See `hwlocality_bitmap(3)
    <https://www.open-mpi.org/projects/hwloc/doc/v2.9.0/a00181.php>`_,
    especially the :func:`hwloc_bitmap_list_snprintf`,
    :func:`hwloc_bitmap_snprintf` and :func:`hwloc_bitmap_taskset_snprintf`
    functions.

  verbose
    Log the cpuset assigned to the shell and each task (if :option:`per-task`
    is used). When combined with options listed above, the :option:`verbose`
    option must come first followed by a comma.

  dry-run
   Print cpusets but do not actually apply CPU affinity bindings. Useful
   for testing and debugging affinity configurations. Implies
   :option:`verbose`. When used with options listed above, the
   :option:`dry-run` option must come first followed by a comma.


.. option:: gpu-affinity=OPT

  Adjust operation of the builtin shell ``gpubind`` plugin.  This plugin
  sets :envvar:`CUDA_VISIBLE_DEVICES` to the GPU IDs allocated to the job.
  If the option is unspecified, ``on`` is assumed.  *OPT* may be set to:

  on
    Constrain each task to the full set of GPUs allocated to the job.

  off
    Disable the gpu-affinity plugin.

  per-task
    Constrain each task to an evenly populated subset of the GPUs allocated
    to the job.  Tasks share GPUs only if there are more tasks than GPUs.

  map:LIST
    Constrain each task to *LIST*, a semicolon-delimited list of GPUs.
    See :option:`cpu-affinity` above for a description of *LIST* format.

.. option:: stop-tasks-in-exec

  Stops tasks in ``exec()`` using ``PTRACE_TRACEME``. Used for debugging
  parallel jobs. Users should not need to set this option directly.

.. option:: oom.adjust=VALUE

  Adjust each task's OOM score to make it more or less likely to be
  killed when system memory is critically low.  A value of 1000 maximizes
  the task's probability of being selected, while a value of -1000 would
  prevent the task from being selected.  However, setting a negative value
  is normally a privileged operation.

  For more information, refer to :option:`oom_score_adj` in
  :linux:man5:`proc`.

.. option:: output.{stdout,stderr}.type=TYPE

  Set job output to for **stderr** or **stdout** to *TYPE*. *TYPE* may
  be one of ``term``, ``kvs`` or ``file`` (Default: ``kvs``). If only
  ``output.stdout.type`` is set, then this option applies to both
  ``stdout`` and ``stderr``. If set to ``file``, then ``output.<stream>.path``
  must also be set for the stream.

  See also: :option:`flux-submit --output`, :option:`flux-submit --error`.

.. option:: output.limit=SIZE

   Truncate KVS output after SIZE bytes have been written. SIZE may
   be a floating point value with optional SI units k, K, M, G.  The maximum
   value is 1G.  The default KVS output limit is 10M for jobs
   in a multi-user instance or 1G for single-user instance jobs.
   This value is ignored if output is directed to a file.

.. option:: output.{stdout,stderr}.path=PATH

  Set job stderr/out file output to PATH.

.. option:: output.mode=truncate|append

  Set the mode in which output files are opened to either truncate or
  append. The default is to truncate.

.. option:: output.client.{lwm,hwm}=N

  Set the high and low watermark values used for output flow control
  when output is being aggregated on the leader shell.  Output is aggregated
  by default, unless it is redirected to per-rank local files.

  Flow control limits the growth of message backlogs on the leader shell
  using a credit-based protocol. Each shell starts with credits equal to
  the high watermark. When credits drop to the low watermark, the shell
  requests more credits from the leader. When credits reach zero, task
  output handling is stopped, potentially stalling tasks once their
  local output buffers are exhausted. When more credits are received
  from the leader, task output handling resumes.

  The default values of 100 and 1000 should be adequate in most cases.

.. option:: input.stdin.type=TYPE

  Set job input for **stdin** to *TYPE*. *TYPE* may be either ``service``
  or ``file``.

  See also: :option:`flux-submit --input`.

.. option:: exit-timeout=VALUE

  When the first task in a job exits, and exit-timeout is not set to
  ``none``, a timer is initiated with a duration specified by *VALUE*. If the
  timer expires, a fatal exception is raised on the job. The timeout
  duration can be adjusted by specifying a new *VALUE* in Flux Standard
  Duration format. Setting the value to "none" disables both the timer
  and the exception. The default timeout is "30s" for normal parallel jobs
  (:man1:`flux-submit` and :man1:`flux-run`) and "none" for jobs that are
  instances of Flux (:man1:`flux-batch` and :man1:`flux-alloc`).

.. option:: exit-on-error

  If the first task to exit was signaled or exited with a nonzero status,
  raise a fatal exception on the job immediately.

.. option:: rlimit

  A dictionary of soft process resource limits to apply to the job before
  starting tasks. Resource limits are set to integer values by lowercase
  name without the ``RLIMIT_`` prefix, e.g. ``core`` or ``nofile``.

  See also: :option:`flux-submit --rlimit`.

.. option:: taskmap

  Request an alternate job task mapping. This option is an object
  consisting of required key ``scheme`` and optional key ``value``. The
  shell will attempt to call a ``taskmap.scheme`` plugin callback in the
  shell to invoke the alternate requested mapping. If ``value`` is set,
  this will also be passed to the invoked plugin.

  See also: :option:`flux-submit --taskmap`.

.. option:: pmi=LIST

  Specify a comma-separated list of PMI implementations to enable.  If the
  option is unspecified, ``simple`` is assumed.  To disable, set *LIST* to
  ``off``.  Available implementations include

  simple
    The simple PMI-1 wire protocol.  This implementation works by passing an
    open file descriptor to clients via the :envvar:`PMI_FD` environment
    variable.  It must be enabled when Flux's ``libpmi.so`` or ``libpmi2.so``
    libraries are used, and is preferred by :man1:`flux-broker`
    when Flux launches Flux, e.g. by means of :man1:`flux-batch` or
    :man1:`flux-alloc`.

  pmi1, pmi2
    Aliases for ``simple``.

  cray-pals
    Provided via external plugin from the
    `flux-coral2 <https://github.com/flux-framework/flux-coral2>`_ project.

  pmix
    Provided via external plugin from the
    `flux-pmix <https://github.com/flux-framework/flux-pmix>`_ project.

.. option:: pmi-simple.nomap

  Skip pre-populating the ``flux.taskmap`` and ``PMI_process_mapping`` keys
  in the ``simple`` implementation.

.. option:: pmi-simple.exchange.k=N

  Configure the PMI plugin's built-in key exchange algorithm to use a
  virtual tree fanout of ``N`` for key gather/broadcast in the ``simple``
  implementation.  The default is 2.

.. option:: stage-in

  Copy files to the directory referenced by :envvar:`FLUX_JOB_TMPDIR` that
  were previously archived with :man1:`flux-archive`.

.. option:: stage-in.names=LIST

  Select archives to extract by specifying a comma-separated list of names
  If no names are specified, ``main`` is assumed.

.. option:: stage-in.pattern=PATTERN

  Further filter the selected files to copy using a :man7:`glob` pattern.

.. option:: stage-in.destination=[SCOPE:]PATH

  Copy files to the specified destination instead of the directory referenced
  by :envvar:`FLUX_JOB_TMPDIR`.  The argument is a directory with optional
  *scope* prefix.  A scope of ``local`` denotes a local file system (the
  default), and a scope of ``global`` denotes a global file system.  The copy
  takes place on all the job's nodes if the scope is local, versus only the
  first node of the job if the scope is global.

.. warning::
  The directory referenced by :envvar:`FLUX_JOB_TMPDIR` is cleaned up when the
  job ends, is guaranteed to be unique, and is generally on fast local storage
  such as a *tmpfs*.  If a destination is explicitly specified, use the
  ``global:`` prefix where appropriate to avoid overwhelming a shared file
  system, and be sure to clean up.

.. option:: signal=OPTION

  Deliver signal ``SIGUSR1`` to the job 60s before job expiration.
  To customize the signal number or amount of time before expiration to
  deliver the signal, the ``signal`` option may be an object with one
  or both of the keys ``signum`` or ``timeleft``. (See below)

  See also: :option:`flux-submit --signal`.

.. option:: signal.signum=NUMBER

  Send signal *NUMBER* to the job :option:`signal.timeleft` seconds before
  the time limit.

.. option:: signal.timeleft=TIME

  Send signal :option:`signal.signum` *TIME* seconds before job expiration.

.. option:: hwloc.xmlfile

  Write the job shell's copy of hwloc XML to a file and set ``HWLOC_XMLFILE``.
  Note that this option will also unset ``HWLOC_COMPONENTS`` since presence
  of this environment variable may cause hwloc to ignore ``HWLOC_XMLFILE``.

.. option:: hwloc.restrict

  With :option:`hwloc.xmlfile`, restrict the exported topology XML to only
  the resources assigned to the current job. By default the XML is not
  restricted.

.. option:: sysmon

  Log peak memory usage and the cpu load average on each shell at the
  end of the job.  Periodic tracing of memory and cpu load while
  the job is running may be viewed by increasing shell verbosity to
  :option:`verbose=2`.

.. option:: sysmon.period=FSD

  Sysmon tracing is driven by the Flux heartbeat event (normally every
  two seconds).  The polling period may be changed to a different value
  in RFC 23 Flux Standard Duration format.

.. option:: rexec-shutdown-timeout=FSD

  When all tasks exit, processes launched via the rexec server receive
  SIGTERM, then SIGKILL after this timeout (RFC 23 Flux Standard Duration).
  Default: 60s.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-shell`, :man5:`flux-shell-initrc`, :man7:`flux-shell-plugins`
