.. _sdexec:

########################
Systemd Execution Module
########################

The sdexec broker module implements the ``sdexec`` subprocess service,
an alternative to the built-in ``rexec`` service.  It processes
``sdexec.exec`` requests from job-exec and manages the full lifecycle
of a transient systemd unit for each request.  One instance runs per
broker rank.

sdexec uses the libsdexec library and the :doc:`sdbus` module to
communicate with systemd.

When resource containment is enabled (``exec.sdexec-constrain-resources``),
the ``sdexec-mapper`` module works alongside sdexec to translate job
resource allocations into systemd unit properties that restrict jobs to
their allocated CPUs, GPUs, and devices. See :man5:`flux-config-exec` for
configuration details.

*************
sdexec Module
*************

Per-process State (``struct sdproc``)
======================================

Each exec request creates an ``sdproc`` that holds:

- the original request message
- the command JSON object
- futures for start, stop, and property-watch RPCs
- a ``struct unit`` tracking current unit state
- three ``struct channel`` instances for stdin, stdout, and stderr
- stop timer state for kill escalation
- response-sent flags to prevent duplicate responses

Unit Naming
===========

A transient unit is named ``<label>:<instance>.service``, where *label* is
the subprocess label supplied by the client and *instance* is the broker's
``jobid-path`` attribute rendered as a name: the leading separator is
dropped, further separators become hyphens, and job IDs take the plain
f58 form, e.g. ``/sys/ƒJOBID`` becomes ``sys-fJOBID``.  A label never contains
a colon, so the first colon in a unit name separates the label from the
rendered jobid-path, and a unit name maps back to its instance and job
mechanically.  A client that does not set a label is given a truncated
UUID as its label, which is used for the name and reported by
``sdexec.list``, and is kept in the unit record so it is unchanged after a
restart.  A client may override the whole name with the ``SDEXEC_NAME``
command option, which is then used verbatim.

Where a node hosts more than one broker, those brokers share a systemd user
instance, so the broker rank is included and the name becomes
``<label>:<rank>-<instance>.service``.  sdexec decides this
at load time from the RFC 34 taskmap in the ``broker.mapping`` attribute,
and includes the rank if that attribute is unavailable.  Two properties of
the system instance are assumed here: only one *sys* instance runs per user
per node, and a system instance never hosts more than one broker per node,
so ``sys`` alone cannot collide and its units normally carry no rank.
(The system instance name is fixed: a configurable name was considered
and deferred, since co-located system instances would also need distinct
unit files.)

bulk-exec and bgexec label job shells ``<name>-<jobid>``, so in the system
instance a job shell lands in a unit named ``shell-<jobid>:sys.service``.
The job ID makes the name a stable handle for reclaiming a leftover unit
after a module restart.

The systemd user instance is shared by every Flux instance running as this
user, so the *instance* suffix is what keeps one instance's unit names from
colliding with those of another: ``jobid-path`` begins with the name of the
top level instance and is extended at every nesting level, so it is unique
among the instances running as one user on a node.  It also lets a tool
match one instance's units, and no others, with the glob
``*:<instance>.service``.  See :man7:`flux-broker-attributes` for how
``jobid-path`` is formed.

I/O Channels
============

Three :linux:man2:`socketpair` channels are created before the unit is started:

- *in* (stdin) — written by sdexec from incoming write RPCs
- *out* (stdout) — read by sdexec; data forwarded as streaming responses
- *err* (stderr) — read by sdexec; data forwarded as streaming responses

The file descriptors for the systemd side of each pair are passed to
``sdexec_start_transient_unit()`` as the ``stdin_fd``, ``stdout_fd``, and
``stderr_fd`` arguments, and are transmitted to systemd as D-Bus file handle
(``h``) typed arguments in ``StartTransientUnit``.  The Flux side FDs are
retained for reading (stdout/stderr) and writing (stdin).

Both stdout and stderr channels are line-buffered by default (``CHANNEL_LINEBUF``).
When systemd closes its end of a channel upon unit exit, the Flux side sees
EOF.  sdexec waits for both stdout and stderr to reach EOF before sending the
final ``ENODATA`` response that closes the exec stream.

Output data is encoded using ``libioencode`` (stream name + rank + data) and
sent as streaming RPC responses.

Unit Lifecycle
==============

After calling ``sdexec_start_transient_unit()``, sdexec subscribes to
``PropertiesChanged`` signals on the unit's D-Bus object path.  The
following state transitions drive the response protocol:

ACTIVE / RUNNING with ExecMainPID set
   Send started response with PID.

ACTIVE / EXITED with ExecMainCode available
   Send finished response with wait status; call StopUnit.

FAILED
   Send error response with systemd result code.

After ``StopUnit`` is called, sdexec waits for stdout and stderr to reach EOF,
then sends ``ENODATA`` to close the exec stream.

Unit Records
============

Before a unit is started, sdexec writes a *record* for it: a small
eventlog (RFC 18), named after the unit, in the record directory
(``sdexec-record-dir`` in :man5:`flux-config-systemd`, defaulting to the
``XDG_RUNTIME_DIR`` of the user systemd instance that owns the units).  Its
first event is ``start``, whose context carries what a later instance of
the module needs to re-adopt the unit after a restart: the label it is
looked up by, the command line, the background and waitable flags, and the
stop timer settings.  If the record cannot be written, the exec request
fails rather than starting a unit whose status could be lost.

The unit is also given an ``ExecStopPost`` command that appends an
``exit`` event to the record when the main process exits, with a context
of ``result``, ``code`` and ``status`` taken from systemd.  For example::

  {"timestamp":1763312345.12,"name":"start","context":{"label":"job1","cmdline":["flux-shell","1"],"bg":true,"waitable":true,"stop_timer_sec":30,"stop_timer_signal":10}}
  {"timestamp":1763312400.98,"name":"exit","context":{"result":"success","code":"exited","status":"0"}}

systemd runs the command in every terminal case (normal exit, signal
death, administrative stop) with ``SERVICE_RESULT``, ``EXIT_CODE`` and
``EXIT_STATUS`` in its environment, and runs it as the unit's user, so a
guest process in the cgroup cannot forge a status.  The command appends
only to a record that still exists, so a unit whose record sdexec has
already removed leaves nothing behind.  The timestamps show when an
uncollected record was created and how long its unit ran.

The exit event is the durable copy of the exit status.  It matters
because the copy systemd keeps does not always survive: job-exec runs IMP
units with ``Type=notify``, and the IMP sends ``STOPPING=1`` before it
exits, so a unit whose main process then exits 0 goes straight to
*inactive/dead* and is garbage collected along with its status.  (A
nonzero exit or signal death is retained in the *failed* state until
reset, and a ``Type=exec`` unit's successful exit is held in
*active/exited* by ``RemainAfterExit``.)  With the exit event, the status
survives regardless of what systemd does with the unit.  An exit event
that cannot be read (for example, one cut short because the command was
killed) leaves the status unknown, and is treated as lost rather than as
a clean exit.

sdexec removes a unit's record when it is finished with the
process: when a foreground or non-waitable unit is reaped, or when a
waitable one has delivered its status to a ``wait``.  Records are not
removed at module unload, since their purpose is to outlive the module.  A
record whose status is never collected persists until a reboot clears the
runtime directory; it is reported as a zombie by ``sdexec.list`` after
every restart until an operator cleans it up.

Stop Timer and Kill Escalation
===============================

If a process does not exit on its own, the stop timer provides SIGTERM-to-SIGKILL
escalation:

1. When the unit enters DEACTIVATING state, the stop timer is armed
   (disabled by default; configured by ``kill-timeout`` in
   :man5:`flux-config-exec`).
2. On first expiry: ``KillUnit`` with SIGTERM is sent; timer is reset.
3. On second expiry: ``KillUnit`` with SIGKILL is sent; timer is reset.
4. On third expiry: the request is failed with ``EDEADLK``.

Background Execution
====================

An ``sdexec.exec`` request initiated as a non-streaming RFC 6 request runs
in *background mode* (RFC 42).  Rather than streaming output and a terminating
status back to the client, sdexec sends a single ``started`` response and then
detaches: the transient unit continues to run until it exits or the module is
unloaded.  As required by RFC 42, a background unit's stdin is at end-of-file,
so a process that reads stdin does not block waiting for a client that will
never write.

A background unit's stdout and stderr are captured regardless of the
``stdout``/``stderr`` flags (which only gate streaming to a client).  Each
output line is written to the broker log — stdout at ``LOG_INFO`` and stderr
at ``LOG_ERR``, prefixed with the process name and PID — so the log is a
complete record of a process whose output is not streamed.  The process's exit
status is logged the same way.  Any terminal error that occurs after the
``started`` response (and so cannot be returned on the exec request) is also
logged rather than silently dropped.  This mirrors the built-in rexec server.

Waitable Processes
==================

A background process started with the ``waitable`` flag can be waited on with
an ``sdexec.wait`` request that returns its exit status.  This lets job-exec
run a job in the background and collect its result separately, decoupling the
start and wait phases per RFC 42.

While a waitable process runs, sdexec retains a bounded tail of its most
recent output (up to ``RETAINED_OUTPUT_MAX`` bytes, oldest dropped first) as an
array of I/O objects.  The ``wait`` response carries the exit status and, if
any was retained, this output, so job-exec can record it in the job's KVS
output eventlog.

A ``wait`` request identifies its target by ``pid`` or, if given, ``label``.
Its handling depends on the process state:

- If the process has already terminated, the status (and any retained output)
  is returned immediately and the process is removed.
- If it is still running, the request is *parked*: it is answered when the
  unit is reaped.  Only one waiter is allowed at a time.
- If a terminal error occurred after the process started, that error is
  returned to the waiter — a successful background start is never reported to a
  later ``wait`` as if the process had never existed.

If the client sending a parked ``wait`` disconnects, the parked request is
dropped, but the process remains waitable so its status is not lost and a
subsequent ``wait`` can still collect it.  A waitable process that is never
waited on is retained until the module is unloaded, and its record (see
`Unit Records`_ above) carries its status across the restart (see
`Recovery After a Module Restart`_ below).

Inspecting and Signaling Processes
==================================

The ``sdexec.list`` RPC returns the processes sdexec is tracking, each with
its pid, command, label, and state (``R`` while the unit is running, ``Z``
once it has finished but is retained awaiting a ``wait``).  The
``sdexec.kill`` RPC signals a process identified by ``pid`` or, if given,
``label``.  Both are surfaced by :man1:`flux-sproc` with ``--service sdexec``.

Recovery After a Module Restart
===============================

Transient units keep running when the sdexec module is unloaded, so a module
(or broker) restart can leave units behind.  At startup, sdexec sweeps for
leftovers from its previous incarnation and re-adopts each as an ordinary
tracked process so that job-exec can reclaim it by label with a ``wait``
request.  The sweep considers the union of two sets, both filtered by this
instance's unit name suffix (see `Unit Naming`_ above), which finds this
units of this sdexec on this rank and no others:

- The units loaded in the user systemd instance.  Each running or failed
  one is adopted with its live state, monitored by the same per-unit
  property watch as a unit started normally.  A ``GetAll`` snapshot over
  every interface supplies both the unit state and the exit status in one
  reply, taken after the watch is installed so no transition is missed in
  between.  A unit with no record (started by another tool, or before
  records existed) is adopted as background and waitable, with its label
  derived from the unit name, so its status can still be collected.
- The records in the record directory (see `Unit Records`_ above) whose
  units are gone, or are still loaded but inactive.  If systemd appended an
  exit event, the process is adopted
  already finished, and a later ``wait`` collects the status; a
  non-waitable one has nobody to collect it, so its exit is logged and the
  record removed.  If there is no readable exit event, the unit died
  without running its stop path, and the process is *lost*: it is adopted
  with a terminal error so that a ``wait`` fails rather than parking
  forever, and never mistakes the loss for a clean exit.  This happens if
  the user systemd instance is killed outright.  An orderly restart of
  the instance stops each unit, which runs its stop path, but an IMP
  unit that does not exit within the stop timeout of ``user@.service``
  is killed along with the instance, without one.  Any
  temporary file left by a record write interrupted by a crash is
  removed.

The sweep runs in the background, so the module finishes loading even if
the user bus is slow to come up, but until the sweep completes sdexec
holds every ``exec``, ``write``, ``kill``, ``wait`` and ``list`` request,
and answers them in arrival order once it has.  So sdexec always answers
from an authoritative process table: a *not found* error means the
process does not exist, never that it has not been swept yet.  There is
no deadline: a user bus that never answers leaves those requests pending,
as it would leave them pending without the sweep, and a warning is
logged if the sweep is still waiting after 30 seconds.

Because the stdio channels died with the previous module instance, no
output can be retained for a recovered process: the ``wait`` response
carries the exit status only.

********************
sdexec-mapper Module
********************

The sdexec-mapper module translates job resource allocations (cores, GPUs)
into systemd unit properties when ``exec.sdexec-constrain-resources`` is
enabled. It runs on each broker rank alongside the sdexec module.

Resource Mapping Process
=========================

For each job start request, sdexec-mapper:

1. Extracts the local rank's resources from the job's R (resource set)
2. Calls ``map_<type>()`` methods for each resource type (cores, gpus, etc.)
3. Calls ``finalize_properties()`` to add general properties not tied to
   specific resource types
4. Returns the complete property dict to sdexec for inclusion in the
   StartTransientUnit D-Bus call

The mapper receives ``exec.sdexec-properties`` as additional context.
The default ``HwlocMapper`` uses this to scale memory cap properties
(``MemoryHigh``, ``MemoryMax``, ``MemorySwapMax``) proportional to the
job's processing unit allocation; custom mappers may use it for
site-specific adjustments.  Mapper-generated properties take precedence
over ``sdexec-properties`` values.

The default HwlocMapper implementation:

- Uses hwloc topology XML to map logical core IDs to physical CPUs and
  NUMA nodes
- Discovers GPU device nodes via sysfs based on PCI addresses from hwloc
- Sets DevicePolicy=closed to enforce device containment
- Scales memory cap properties (``MemoryHigh``, ``MemoryMax``,
  ``MemorySwapMax``) from ``exec.sdexec-properties`` by the ratio of
  allocated to total processing units, so jobs sharing a node each receive
  a proportional memory limit

Property Generation
===================

The mapper generates systemd unit properties that enforce resource constraints:

AllowedCPUs
   CPU affinity mask restricting the job to allocated physical CPUs.
   Derived from logical core IDs via hwloc topology.

AllowedMemoryNodes
   NUMA node affinity mask restricting memory allocations to nodes
   associated with allocated cores.

DeviceAllow
   Allow list of device paths the job may access. For GPUs, includes
   vendor-specific devices (e.g., /dev/nvidia0, /dev/kfd) and DRM
   render nodes (/dev/dri/renderD*).

DevicePolicy
   Set to "closed" to allow standard pseudo devices (/dev/null, etc.)
   while blocking access to physical devices unless explicitly allowed
   via DeviceAllow.

Module Statistics
=================

The mapper module provides runtime statistics via ``flux module stats sdexec-mapper``:

.. code-block:: json

   {
     "config": {
       "mapper_class": "flux.sdexec.map.HwlocMapper",
       "mapper_searchpath": "",
       "allowed_device_patterns": [ "/dev/cxi*" ],
       "allowed_devices": [ "/dev/cxi0 rw", "/dev/cxi1 rw" ]
     },
     "requests": 42
   }

``allowed_device_patterns`` is the configured ``sdexec.allowed-devices`` list
and ``allowed_devices`` is that list expanded against the local ``/dev``, so
the two together show which patterns matched hardware on this node.

Configuration
=============

See the ``[sdexec]`` table in :man5:`flux-config-exec` for mapper
configuration options, including how to specify custom mapper classes
and search paths.
