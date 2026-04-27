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

Each transient unit is given a unique name derived from a UUID, with the
Flux job ID embedded for observability.  The name has a ``.service`` suffix
as required by systemd.

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

Properties from ``exec.sdexec-properties`` configuration are merged with mapper
output, with mapper values taking precedence. This allows custom mappers to
override configured properties based on job resources (e.g., setting
``MemoryMax`` proportional to allocated cores).

The default HwlocMapper implementation:

- Uses hwloc topology XML to map logical core IDs to physical CPUs and
  NUMA nodes
- Discovers GPU device nodes via sysfs based on PCI addresses from hwloc
- Sets DevicePolicy=closed to enforce device containment

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
     "requests": 42,
     "map_time_ms": {
       "mean": 1.2,
       "min": 0.8,
       "max": 3.5
     }
   }

Configuration
=============

See the ``[sdexec]`` table in :man5:`flux-config-exec` for mapper
configuration options, including how to specify custom mapper classes
and search paths.
