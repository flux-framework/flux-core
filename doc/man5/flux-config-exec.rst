===================
flux-config-exec(5)
===================


DESCRIPTION
===========

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

sdexec-properties
   (optional) A table of systemd properties to set for all jobs.  All values
   must be strings. See :ref:`sdexec_properties` below.

sdexec-stop-timer-sec
   (optional) Configure the length of time in seconds after a unit enters
   deactivating state when it will be sent the sdexec-stop-timer-signal.
   Deactivating state is entered by ``imp-shell`` units when the
   :man1:`flux-shell` terminates.  The unit may remain there as long as
   user processes remain in the unit's cgroup.

   After the same length of time, if the unit hasn't terminated, for example
   due to unkillable processes, the unit is abandoned and the node is drained.
   Default 30.

sdexec-stop-timer-signal
   (optional) Configure the signal used by the stop timer.  By default,
   10 (SIGUSR1, the IMP proxy for SIGKILL) is used.

kill-timeout
   (optional) The amount of time in FSD to wait after ``SIGTERM`` is
   sent to a job before sending ``SIGKILL``. The default is "5s". See
   :ref:`job_termination` below for details.

max-kill-count
   (optional) The maximum number of times ``kill-signal`` will be sent to the
   job shell before the execution system considers the job unkillable and
   drains the node. The default is 8. See :ref:`job_termination` below for
   details.

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
   nodes on which the barrier is waiting.  To disable the barrier timeout,
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
   (options) A table of keys (see :ref:`testexec`) for configuring the
   **job-exec** test execution implementation (used in mainly for testing).


.. _sdexec_properties:

SDEXEC PROPERTIES
=================

When the sdexec service is selected, systemd unit properties may be set by
adding them to the ``sdexec-properties`` sub-table.  All values must be
specified as a TOML strings.  Properties that require other value types
can only be specified if Flux knows about them so it can perform type
conversion.  Those are:

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
   See the :linux:man5:`systemd.exec` for more information.
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

.. _job_termination:

JOB TERMINATION
===============

When a job is canceled or gets a fatal exception it is terminated using
the following sequence

 - The job shells are notified to send ``term-signal`` to job tasks, unless
   the job is being terminated due to a time limit, in which case ``SIGALRM``
   is sent instead.
 - After ``kill-timeout``, job shells are notified to send ``kill-signal`` to
   tasks. This repeats every ``kill-timeout`` seconds.
 - After a delay of ``5*kill-timeout``, the job execution system transitions
   to sending ``kill-signal`` to the job shells directly.
 - This continues with an exponential backoff starting at ``kill-timeout``,
   with the timeout doubling after each attempt (capped at 300s).
 - After a total of ``max-kill-count`` attempts to signal the job shell,
   any nodes still running processes are drained with the message: "unkillable
   user processes for job JOBID."

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

   [exec.testexec]
   allow-guests = true


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_15`


SEE ALSO
========

:man5:`flux-config`,
`systemd.resource-control(5) <https://www.freedesktop.org/software/systemd/man/systemd.resource-control.html>`_
