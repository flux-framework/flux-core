************
Installation
************

System Prerequisites
====================

`MUNGE <https://github.com/dun/munge>`_ is used to sign job requests
submitted to Flux, so the MUNGE daemon should be installed on all
nodes running Flux with the same MUNGE key used across the cluster.

System clocks must be synchronized across the cluster, e.g. with
`Network Time Protocol <https://en.wikipedia.org/wiki/Network_Time_Protocol>`_.

Flux assumes a shared UID namespace across the cluster.

A system user named ``flux`` is required.  This user need not have a valid
home directory or shell.

Flux uses `hwloc <https://www.open-mpi.org/projects/hwloc/>`_ to verify that
configured resources are present on nodes.  Ensure that the system installed
version includes any plugins needed for the hardware, especially GPUs.

A Word about Core Dumps
-----------------------

It is helpful to enable core dumps from the system instance ``flux-broker``
(especially rank 0) so that useful bug reports can be filed should the broker
crash.  Usually :linux:man8:`systemd-coredump` handles this, which makes core
files and stack traces accessible with :linux:man1:`coredumpctl`.

Some sites choose instead to configure the ``kernel.core_pattern``
:linux:man8:`sysctl` parameter to a relative file path, which directs core
files to the program's current working directory.  Please note that the system
instance broker runs as the ``flux`` user with a working directory of ``/``
and thus would not have write permission on its current working directory.
This can be worked around by installing a systemd override file, e.g.

.. code-block::

  # /etc/systemd/system/flux.service.d/override.conf
  [Service]
  WorkingDirectory=/var/lib/flux
  LimitCORE=infinity:infinity

.. note::
  If you do observe a ``flux-broker`` crash, please open a github issue at
  https://github.com/flux-framework/flux-core/issues and include the Flux
  version, relevant log messages from ``journalctl -u flux``, and a stack
  trace, if available.

Installing Software Packages
============================

The following Flux framework packages are needed for a Flux system instance
and should be installed from your Linux distribution package manager.

flux-security
  APIs for job signing, and the IMP, a privileged program for starting
  processes as multiple users. Install on all nodes (required).  If building
  flux-security from source, be sure to configure with ``--enable-pam`` to
  include Pluggable Authentication Modules (PAM) support.

flux-core
  All of the core components of Flux, including the Flux broker.
  flux-core is functional on its own, but cannot run jobs as multiple users,
  has a simple FIFO scheduler, and does not implement accounting-based job
  prioritization. If building flux-core from source, be sure to configure with
  ``--with-flux-security``. Install on all nodes (required).

flux-sched
  The Fluxion graph-based scheduler.

flux-accounting (optional)
  Management of limits for individual users/projects, banks, and prioritization
  based on fair-share accounting.  For more information on how to configure
  run flux-accounting, please refer to the `Flux Accounting Guide <https://flux-framework.readthedocs.io/en/latest/guides/accounting-guide.html>`_.

flux-pam (optional)
  A PAM module that can enable users to login to compute nodes that are
  running their jobs.

.. note::
    Sites are strongly encouraged to use packages when deploying a system
    installation of Flux.  Source RPM packages are maintained for the
    `TOSS <https://computing.llnl.gov/projects/toss-speeding-commodity-cluster-computing>`_
    Red Hat Enterprise Linux based Linux distribution and are included with
    github release assets for each new release.
    Open an issue in `flux-core <https://github.com/flux-framework/flux-core>`_
    if you would like to become a maintainer of Flux packages for another Linux
    distribution so we can share packaging tips and avoid duplicating effort.
