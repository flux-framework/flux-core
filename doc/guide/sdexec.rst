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

systemd requires each transient unit to have a unique name.  For job shells,
bulk-exec supplies one of the form ``<name>-<rank>-<jobid>`` (e.g.
``shell-0-fuzzybunny``); the rank keeps it unique when multiple brokers share
a node, and the job ID makes the name a stable handle for reclaiming a
leftover unit after a module restart.  If no name is supplied, sdexec falls
back to one derived from a truncated UUID.  The name has a ``.service``
suffix as required by systemd.

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
waited on is retained until the module is unloaded.

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
(or broker) restart can leave units behind.  At startup, sdexec sweeps the
user systemd instance for leftover units matching the names bulk-exec uses
(``shell-`` and ``imp-shell-`` prefixes) and adopts each running one as an
ordinary tracked process, monitored by the same per-unit property watch as a
live unit.  Its state is seeded from the unit list; a ``GetAll`` snapshot then
supplies the exit status of a unit that has already exited (preserved by
``RemainAfterExit``).

job-exec reclaims a recovered process with a ``wait`` request by label.
Because the stdio channels died with the previous module instance, no output
can be retained: the ``wait`` response carries the exit status only.
A recovered process also has no command line, so ``sdexec.list`` reports
the unit name in its place.

sdexec owns the *user-bus clean* decision that gates node availability: once
the startup sweep is complete and no recovered process is an *unreclaimed
orphan* (still running, with no waiter attached), it sends a one-shot
``sdmon.user-clean`` request to the sdmon module on the local rank.  A
reclaimed unit stops blocking as soon as a waiter attaches, so the node comes
online while the legitimate job keeps running; a true orphan blocks until it
exits.  sdmon monitors only the system systemd instance (housekeeping,
prolog, epilog) and joins the ``sdmon.online`` broker group, making the node
eligible for scheduling when systemd support is enabled (only after its own
system units are drained *and* the user-clean signal has arrived).  Because a
request to an unloaded module is lost rather than queued, sdmon must be
loaded before sdexec (enforced by modprobe ordering).

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
       "mapper_searchpath": ""
     },
     "requests": 42
   }

Configuration
=============

See the ``[sdexec]`` table in :man5:`flux-config-exec` for mapper
configuration options, including how to specify custom mapper classes
and search paths.
