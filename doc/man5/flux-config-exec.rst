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
   must be strings.  See SDEXEC PROPERTIES below.

kill-timeout
   (optional) The amount of time to wait after ``SIGTERM`` is sent to a job
   before sending ``SIGKILL``.

term-signal
   (optional) Specify an alternate signal to ``SIGTERM`` when terminating
   job tasks. Mainly used for testing.

kill-signal
   (optional) Specify an alternate signal to ``SIGKILL`` when killing tasks
   and the job shell. Mainly used for testing.

testexec
   (options) A table of keys (see :ref:`testexec`) for configuring the
   **job-exec** test execution implementation (used in mainly for testing).


SDEXEC PROPERTIES
=================

When the sdexec service is selected, The following systemd unit properties may
be set by adding them to the ``sdexec-properties`` table:

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


.. _testexec:

TESTEXEC
========

allow-guests
  Boolean value enables access to the testexec implementation from guest
  users. By default, guests cannot use this implementation.

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
