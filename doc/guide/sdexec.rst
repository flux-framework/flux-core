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

.. _post-start-checks:

Post-Start Checks
=================

Once a unit enters the running state, sdexec runs a series of
post-start checks before sending the started response to the caller.
All checks are dispatched through ``sdproc_post_start_checks()``.
If any check fails, the exec request is failed with ``EIO`` and the
error message is logged.

Currently the only post-start check is the AllowedCPUs check, which
is active when ``SDEXEC_CORES`` was set on the request:

- Read ``cpuset.cpus`` from the unit's cgroup via
  :func:`cgroup_info_init_pid`.
- Decode it as a Flux idset and compare to the expected CPU set derived
  from ``SDEXEC_CORES``.
- If the file is absent or the idsets do not match, fail the request.
  The most common cause is the cpuset cgroup controller not being
  delegated to the user systemd instance.

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

Command Options
===============

The sdexec module recognizes the following virtual options in the ``opts``
dict of the JSON command object.  Callers set them via
:func:`flux_cmd_setopt`.  ``SDEXEC_NAME`` and ``SDEXEC_PROP_*`` are
handled by ``sdexec_start_transient_unit()``; see :doc:`libsdexec` for
their full specification.

``SDEXEC_NAME``
  Name of the transient systemd unit, including the ``.service`` suffix.
  Required by ``sdexec_start_transient_unit()``; auto-generated from a
  truncated UUID if not set by the caller.

``SDEXEC_CORES`` *idset*
  A Flux idset string of logical core indices (0-origin, as in R_lite) to
  restrict the unit to.  sdexec expands the core indices to OS CPU (PU)
  indices using the local hwloc topology loaded at module init, then sets
  ``SDEXEC_PROP_AllowedCPUs`` to the result and ``SDEXEC_PROP_AllowedMemoryNodes``
  to the NUMA nodes that cover those CPUs.  A post-start check verifies that
  the kernel applied the CPU constraint; see :ref:`post-start-checks`.

``SDEXEC_STOP_TIMER_SEC`` *seconds*
  Arm the stop timer with a timeout of *seconds*.  When the unit enters
  DEACTIVATING state, the timer delivers ``SIGKILL`` (or the signal in
  ``SDEXEC_STOP_TIMER_SIGNAL``), then after a second expiry abandons the
  unit and fails the request with ``EDEADLK``.  A negative value disables
  the timer (default).

``SDEXEC_STOP_TIMER_SIGNAL`` *number*
  Numerical signal delivered on the first stop-timer expiry instead of
  ``SIGKILL`` (signal 9).

``SDEXEC_PROP_``\ *NAME* *value*
  Set the systemd transient unit property *NAME* to *value*.  Properties
  with special D-Bus type handling are listed in :doc:`libsdexec`.  All
  others are passed as strings.  See :linux:man5:`systemd.resource-control`
  for semantics.

``SDEXEC_TEST_EXPECTED_CPUS`` *idset*
  *Test only — requires* ``sdexec-debug = true``.  Overrides the expected
  CPU idset used by the post-start AllowedCPUs check.  Set to an idset
  that cannot match ``cpuset.cpus`` (e.g. ``"99999"``) to force the check
  to fail for end-to-end testing of the constraint-failure detection path.
