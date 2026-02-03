===================
flux-config-exec(5)
===================


DESCRIPTION
===========

The exec system is highly configurable. If configuring a Flux system instance
for the first time, it may be helpful to consult the Flux Administrator's
Guide (see `RESOURCES`_) and start with a simple configuration. See also
`EXAMPLES`_ below.

The Flux system instance **job-exec** service requires additional
configuration via the ``exec`` table, for example to enlist the services
of a setuid helper to launch jobs as guests.

The ``exec`` table may contain the following keys:


KEYS
====

imp
   (optional) Set the path to the IMP (Independent Minister of Privilege)
   helper program, as described in RFC 15, so that jobs may be launched with
   the credentials of the guest user that submitted them.  If unset, only
   jobs submitted by the instance owner may be executed.

service
   (optional) Set the remote subprocess service name. (Default: ``rexec``).
   Note that ``systemd.enable`` must be set to ``true`` if ``sdexec`` is
   configured.  See :man5:`flux-config-systemd`.

service-override
   (optional) Allow ``service`` to be overridden on a per-job basis with
   ``--setattr system.exec.bulkexec.service=NAME``.  (Default: ``false``).

job-shell
   (optional) Override the compiled-in default job shell path.

kill-timeout
   (optional) The amount of time in Flux Standard Duration (FSD) to wait
   after ``SIGTERM`` is sent to a job before sending ``SIGKILL``. FSD is
   a human-readable time format supporting units like "5s" (seconds),
   "2m" (minutes), "1h" (hours), "3d" (days). See RFC 23 for complete
   specification.  The default is "5s" (5 seconds). See :ref:`job_termination`
   below for details.

max-kill-count
   (optional) The maximum number of times ``kill-signal`` will be sent to the
   job shell before the execution system considers the job unkillable and
   drains the node. The default is 8. Note that the node is drained
   immediately after the final kill attempt without waiting an additional
   timeout period. See :ref:`job_termination` below for details.

max-kill-timeout
   (optional) The maximum amount of time in FSD to wait for a job to terminate
   before draining nodes with unkillable processes. When set, this overrides
   ``max-kill-count`` by continuing the kill signal escalation sequence until
   the specified duration has elapsed since termination began. The default is
   unset (use ``max-kill-count`` instead). See :ref:`job_termination` below
   for details.

   Example: ``max-kill-timeout = "30m"`` gives jobs up to 30 minutes to
   respond to termination signals before affected nodes are drained.

.. note::

  Choosing between max-kill-count and max-kill-timeout:

  Use ``max-kill-timeout`` when you want to specify how long to wait
  before giving up on a job, such as enforcing a site policy that jobs
  get 30 minutes to clean up before nodes are drained. This is simpler
  and more intuitive than calculating the number of kill attempts needed.

  Use ``max-kill-count`` when you want fine-grained control over the
  number of escalation attempts regardless of timing, or when maintaining
  backward compatibility with existing configurations.

  When both are set, ``max-kill-timeout`` takes precedence.

term-signal
   (optional) A string specifying an alternate signal to ``SIGTERM`` when
   terminating job tasks. Mainly used for testing.

kill-signal
   (optional) A string specifying an alternate signal to ``SIGKILL`` when
   killing tasks and the job shell. Mainly used for testing.

barrier-timeout
   (optional) Specify the default job shell start barrier timeout in FSD.
   All multi-node jobs enter a barrier at startup once the Flux job shell
   completes initialization tasks such as changing the working directory
   and processing the initrc file. Once the first node enters this barrier,
   the job execution system starts a timer, and if the timer expires
   before the barrier is complete, raises a job exception and drains the
   nodes on which the barrier is waiting. To disable the barrier timeout,
   set this value to ``"0"``. (Default: ``30m``).

max-start-delay-percent
   (optional) Specify the maximum allowed delay, as a percentage of a job's
   duration, between when a job is allocated (i.e. the starttime recorded
   in _R_) and when the execution system receives the start request from
   the job manager. If the delay exceeds this percentage, then extend the
   job's effective expiration by the delay. This prevents short duration
   jobs from having their runtime significantly reduced, while avoiding a
   differential between the actual resource set expiration and the time
   at which a ``timeout`` exception is raised for longer running jobs,
   where any runtime impact will be negligible. The default is 25 percent.

testexec
   (optional) A table of keys (see :ref:`testexec`) for configuring the
   **job-exec** test execution implementation (used mainly for testing).


.. _sdexec_configuration:

SDEXEC CONFIGURATION
====================

When using the systemd execution service (``service = "sdexec"``), additional
configuration options control how systemd manages job units and interacts with
job-exec's termination sequence. See :ref:`sdexec_timing` for important
information about how these settings interact with the job termination sequence
described in :ref:`job_termination`.

.. tip::
   The current configuration values and derived settings can be inspected at
   runtime using ``flux module stats job-exec``. See :ref:`introspection`
   for details.

sdexec-properties
   (optional) A table of systemd properties to set for all jobs. All values
   must be strings. See :ref:`sdexec_properties` below.

sdexec-stop-timer-sec
   (optional) Configure the length of time in seconds after a unit enters
   deactivating state when it will be sent the ``sdexec-stop-timer-signal``.
   Deactivating state is entered by ``imp-shell`` units when the
   :man1:`flux-shell` terminates.  The unit may remain there as long as
   user processes remain in the unit's cgroup.

   After the same length of time, if the unit hasn't terminated, for example
   due to unkillable processes, the unit is abandoned and the node is drained.

   **Default:** The effective max-kill-timeout value rounded up to the nearest
   integer (see :ref:`job_termination` and :ref:`sdexec_timing`). For example,
   if the effective max-kill-timeout is 1220.5 seconds, the default
   sdexec-stop-timer-sec will be 1221 seconds. This ensures systemd waits at
   least ``2*max-kill-timeout`` (one period before sending SIGKILL, one period
   before abandoning the unit) before draining the node, allowing job-exec's
   normal termination sequence to complete. This can be overridden by
   explicitly setting this value.

   Use ``flux module stats job-exec`` to inspect the current effective value.
   See :ref:`introspection`.

sdexec-stop-timer-signal
   (optional) Configure the signal used by the stop timer.  By default,
   10 (SIGUSR1, the IMP proxy for SIGKILL) is used.


.. _sdexec_properties:

SDEXEC PROPERTIES
=================

When the sdexec service is selected, systemd unit properties may be set by
adding them to the ``sdexec-properties`` sub-table. All values must be
specified as TOML strings. Properties that require other value types
can only be specified if Flux knows about them so it can perform type
conversion. Those are:

MemoryMax
   Specify the absolute limit on memory used by the job, in bytes. The value
   may be suffixed with K, M, G or T, to multiply by Kilobytes, Megabytes,
   Gigabytes, or Terabytes (base 1024), respectively. Alternatively, a
   percentage of physical memory may be specified.  If assigned the special
   value "infinity", no memory limit is applied.

MemoryHigh
   Specify the throttling limit on memory used by the job.  Values are
   formatted as described above.

MemoryMin, MemoryLow
   Specify the memory usage protection of the job.  Values are formatted as
   described above.

MemorySwapMax
   Specify the absolute limit on swap used by the job.  Values are formatted as
   described above.

OOMScoreAdjust
   Sets the adjustment value for the Linux kernel's OOM killer score.
   Values range from -1000 to 1000, with 1000 making a process most likely
   to be selected, and -1000 preventing a process from being selected.
   See :linux:man5:`systemd.exec` for more information.
   Setting a negative value is likely a privileged operation in the Flux
   systemd instance.

The following unit properties are reserved for use by Flux and should not be
added to ``sdexec-properties``: AllowedCPUs, Description, Environment,
ExecStart, KillMode, RemainAfterExit, SendSIGKILL, StandardInputFileDescriptor,
StandardOutputFileDescriptor, StandardErrorFileDescriptor, TimeoutStopUSec,
Type, WorkingDirectory.


.. _testexec:

TESTEXEC
========

allow-guests
   Boolean value enables access to the testexec implementation from guest
   users. By default, guests cannot use this implementation.


.. _introspection:

CONFIGURATION INTROSPECTION
===========================

The current effective configuration and some other runtime statistics can
be queried using ``flux module stats job-exec``. This is useful for:

- Verifying configuration after changes
- Understanding derived/calculated values
- Debugging timing issues
- Monitoring job execution system behavior

Key configuration settings available:

**Termination Settings:**

kill-timeout
   The configured kill timeout value

term-signal, kill-signal
   The signals used for termination

max-kill-count
   The configured maximum kill attempt count

max-kill-timeout
   The configured max-kill-timeout (0 if unset)

effective-max-kill-timeout
   The calculated maximum time the execution system will wait before draining
   nodes. This is either the configured ``max-kill-timeout`` or a value
   derived from ``max-kill-count`` and the exponential backoff sequence.
   This value is reported as a floating-point number (e.g., 1220.5 seconds).

   When using ``max-kill-count``, this represents the time from the start of
   job termination until the final kill attempt is sent. The node is drained
   immediately after this final attempt without waiting an additional timeout
   period.

**Sdexec Settings** (under ``bulk-exec.config``):

sdexec_stop_timer_sec
   The effective stop timer value in seconds. If not explicitly configured,
   this will equal ``effective-max-kill-timeout``.

sdexec_stop_timer_signal
   The signal number used by the stop timer

**Example:**

::

   $ flux module stats job-exec
   {
    "kill-timeout": 5.0,
    "term-signal": "SIGTERM",
    "kill-signal": "SIGKILL",
    "max-kill-count": 8,
    "max-kill-timeout": -1.0,
    "effective-max-kill-timeout": 640.0,
    "jobs": {},
    "bulk-exec": {
     "config": {
      "default_cwd": "/tmp",
      "default_job_shell": "/home/grondo/git/f.git/src/shell/flux-shell",
      "exec_service": "rexec",
      "exec_service_override": 0,
      "default_barrier_timeout": 1800.0,
      "sdexec_stop_timer_sec": 640,
      "sdexec_stop_timer_signal": 10
     }
    }
   }


In this example:
- No ``max-kill-timeout`` is configured (shown as -1.0)
- The ``effective-max-kill-timeout`` is 640.0 seconds, calculated from
  ``max-kill-count=8`` with exponential backoff: (5 * 5) + 5 + 10 + 20 + 40
  + 80 + 160 + 300 = 640s (representing 5 * kill-timeout until the first kill,
  then the amount of time until the eighth and final kill attempt)
- The ``sdexec_stop_timer_sec`` defaults to 1220 seconds (the effective
  max-kill-timeout rounded up, though in this case it's already an integer)

**Example with explicit max-kill-timeout:**

::

   $ echo exec.max-kill-timeout=\"30m\" | flux config load
   $ flux module stats job-exec
   {
    "kill-timeout": 5.0,
    "term-signal": "SIGTERM",
    "kill-signal": "SIGKILL",
    "max-kill-count": 8,
    "max-kill-timeout": 1800.0,
    "effective-max-kill-timeout": 1800.0,
    "jobs": {},
    "bulk-exec": {
     "config": {
      "default_cwd": "/tmp",
      "default_job_shell": "/home/grondo/git/f.git/src/shell/flux-shell",
      "exec_service": "rexec",
      "exec_service_override": 0,
      "default_barrier_timeout": 1800.0,
      "sdexec_stop_timer_sec": 1800,
      "sdexec_stop_timer_signal": 10
     }
    }
   }


Here the explicit ``max-kill-timeout`` setting (1800.0 seconds) determines
the effective-max-kill-timeout, and the default sdexec stop timer is set to
this value rounded up (1800 seconds, already an integer in this case).


.. _job_termination:

JOB TERMINATION
===============

When a job is canceled or gets a fatal exception it is terminated using
the following sequence:

 - The job shells are notified to send ``term-signal`` to job tasks, unless
   the job is being terminated due to a time limit, in which case ``SIGALRM``
   is sent instead.
 - After ``kill-timeout``, job shells are notified to send ``kill-signal`` to
   tasks. This repeats every ``kill-timeout`` seconds.
 - After a delay of ``5*kill-timeout``, the job execution system transitions
   to sending ``kill-signal`` to the job shells directly.
 - This continues with an exponential backoff starting at ``kill-timeout``,
   with the timeout doubling after each attempt (capped at 300s).
 - If ``max-kill-timeout`` is set, the execution system continues sending
   ``kill-signal`` to job shells until the specified duration has elapsed
   since termination began, then drains the nodes immediately.
 - If ``max-kill-timeout`` is not set, the execution system uses
   ``max-kill-count`` to limit the number of kill attempts. After the final
   kill attempt, nodes are drained immediately without waiting an additional
   timeout period.
 - In either case, any nodes still running processes for the job are drained
   with the reason: "unkillable user processes for job JOBID."

.. note::
   **Timing calculation with max-kill-count:** The effective max-kill-timeout
   represents the time from termination start until the final kill attempt.
   For example, with ``max-kill-count=4`` and ``kill-timeout=1s``, kill
   attempts occur at 5s, 6s, 8s, and 12s, giving an effective max-kill-timeout
   of 12s. The node is drained at 12s immediately after the final attempt,
   not at 20s (12s + 8s timeout period).

.. note::
   When using sdexec, see :ref:`sdexec_timing` for information about how
   the systemd unit lifecycle interacts with this termination sequence.


.. _sdexec_timing:

SDEXEC AND JOB TERMINATION INTERACTION
=======================================

When using sdexec, the systemd unit lifecycle adds an additional layer to the
job termination process. Understanding this interaction is essential for
configuring appropriate timeouts.

**Job completion with sdexec:**

When a job shell exits (either normally after tasks complete, or due to
job-exec sending signals during exception-based termination):

1. The IMP notifies systemd that the unit is stopping.

2. If no processes remain in the unit's cgroup, the IMP exits and the job
   completes successfully.

3. If processes remain in the cgroup, systemd starts the
   ``sdexec-stop-timer-sec`` countdown.

4. After ``sdexec-stop-timer-sec`` seconds, if processes are still present,
   systemd sends ``sdexec-stop-timer-signal`` (SIGKILL) to them.

5. After another ``sdexec-stop-timer-sec`` seconds, if the unit still hasn't
   terminated, systemd abandons the unit and job-exec drains the node.

**Why the sdexec stop timer is necessary:**

The stop timer is essential for handling the case where the job shell
terminates normally after all tasks exit, but unkillable processes remain in
the cgroup. Without this timer, the job would remain in RUN state indefinitely
(or until its time limit expires), with no mechanism to detect or handle the
problem.

**Why the stop timer must exceed max-kill-timeout:**

During exception-based job termination (cancellation, timeout, etc.), job-exec
may kill the job shell before all tasks have exited. This triggers the sdexec
stop timer sequence described above. If ``sdexec-stop-timer-sec`` were shorter
than the effective max-kill-timeout, systemd would abandon units and drain
nodes before job-exec's termination sequence completes, prematurely giving up
on jobs that might still respond to signals.

By defaulting ``sdexec-stop-timer-sec`` to the effective max-kill-timeout
(rounded up), Flux ensures systemd waits at least ``2*max-kill-timeout``
total (one period before sending SIGKILL, another before abandoning the unit).
This gives job-exec's termination sequence (which takes up to max-kill-timeout)
time to complete, while still providing timely cleanup for the normal
termination edge case.

**Example:** If ``max-kill-timeout = "30m"``, then ``sdexec-stop-timer-sec``
defaults to 1800 seconds (30 minutes). Systemd will wait:

- 30 minutes before sending SIGKILL to remaining processes
- Another 30 minutes (60 minutes total) before abandoning the unit

This ensures the full 30-minute job-exec termination sequence can complete
before systemd intervenes.

**Example with max-kill-count:** If ``max-kill-count = 8`` (default) and
``kill-timeout = 5s`` (default), the effective max-kill-timeout is 640
seconds. The ``sdexec-stop-timer-sec`` defaults to 640 seconds, giving systemd
1280 seconds (about 21 minutes) total before abandoning the unit. Since
job-exec drains immediately after the final kill attempt at 640 seconds,
the longer systemd timeout ensures it doesn't interfere with job-exec's
termination sequence.

Sites with jobs that require extended cleanup time should set
``max-kill-timeout`` appropriately rather than tuning ``sdexec-stop-timer-sec``
directly, as this maintains proper coordination between both systems.


EXAMPLES
========

::

   [exec]
   imp = "/usr/libexec/flux/flux-imp"
   job-shell = "/usr/libexec/flux/flux-shell-special"

::

   [exec]
   service = "sdexec"
   [exec.sdexec-properties]
   MemoryMax = "90%"

::

   [exec]
   # Give jobs 30 minutes to terminate before draining nodes
   max-kill-timeout = "30m"
   service = "sdexec"
   # sdexec-stop-timer-sec will default to 1800 (30 minutes)
   # giving systemd 60 minutes total before abandoning units

::

   [exec]
   service = "sdexec"
   max-kill-timeout = "15m"
   # Override the default if jobs need even more time for cleanup
   sdexec-stop-timer-sec = 1800  # 30 minutes instead of 15

::

   [exec.testexec]
   allow-guests = true


RESOURCES
=========

.. include:: common/resources.rst

Flux Administrator's Guide: https://flux-framework.readthedocs.io/projects/flux-core/en/latest/guide/admin.html


FLUX RFC
========

:doc:`rfc:spec_15`, :doc:`rfc:spec_23`


SEE ALSO
========

:man5:`flux-config`,
`systemd.resource-control(5) <https://www.freedesktop.org/software/systemd/man/systemd.resource-control.html>`_
