.. _admin-guide:

********************
Migrating from Slurm
********************

*I run a large HPC center.  Should I kick Slurm to the curb?*

Not today.  Enjoy your Slurm (it's highly addictive!).
As a reminder, Flux can coexist with Slurm as an enhanced step
manager and portability layer.  See :ref:`start_slurm`.

Check back with us at the end of 2026.

Flux Maturity
=============

Flux is in daily production use as the system workload manager on
`El Capitan <https://hpc.llnl.gov/hardware/compute-platforms/el-capitan>`_
(currently in slot 1 of the `TOP500 <https://www.top500.org/>`_
and its unclassified sister systems at LLNL.  The process of standing up
those systems brought the Flux team together to address problems of
scale and stability with unprecedented urgency.  As a result, system
deployments of Flux on virtually any size system are viable at this point.
That said, the process also brought into focus missing features, now
prioritized.  As of December 2025:

- Binary packages are only published internally for the TOSS operating
  system.  Source RPM packages for RHEL 8 and 9 are manually attached to
  github releases.  Wider operating system support and binary package
  distribution are being discussed.

- All nodes of Flux instance must run the same flux broker release version.
  When flux-core 1.0.0 is released, relaxed version interoperability rules
  will be published.  For now, rolling upgrades are not possible.

- Support for restarting Flux without perturbing running jobs is expected
  late 2026.

- Reservation support is expected mid 2026.

- Flux's flexible resource representation is not yet mature.  Meanwhile,
  hardware locality and binding needs of modern applications can be tricky
  to satisfy in Flux.

- The system Flux does not preserve job :term:`step` data by default.

- Although Flux has an optional local fair-share accounting system for
  gathering usage data and setting job priorities, there is not yet support
  for a site-wide, aggregated accounting system.

- No commercial support or training options.

Flux Design Advantages
======================

Replacing Slurm with Flux may bring substantial long term rewards.
Here are some advantages of Flux's design over Slurm:

Flux has a solid security design
  While a significant amount of Slurm code runs as root, virtually none of
  Flux's does.  For more detail on Flux's security model, refer to
  :ref:`background_security`.

Flux doesn't need a step manager
  Each batch job or allocation is a new (single user) instance of Flux
  with full capability and configurability, and a :term:`step` is really
  a :term:`job` in Flux.  Limitations of Slurm's step manager have been
  a source of long standing problems.

Flux APIs are not an afterthought
  Well thought out Python and C APIs make it easy to integrate Flux with
  workflow systems and new environments.

Flux has a rich resource representation
  Flux's design incorporates a graph-based resource model that is
  extensible to arbitrary resource types.  Caveat: not yet fully implemented.

Flux is dynamic
  Flux supports growing and shrinking allocations.  Caveat: not yet fully
  implemented.

Flux is scalable
  Flux's recursive launch design enables each allocation to scale
  independently.  Each instance of Flux has job throughput and node count
  scalability comparable to Slurm.  But a cluster will typically be running
  many instances of Flux compared to one instance of Slurm.

Flux encourages experimentation
  In addition to extensibility via plugins (see :ref:`background_components`),
  unprivileged users can trivially launch reconfigured or modified single-user
  Flux instances in system resource allocations.

Flux uses reactive messaging
  In contrast to Slurm's multi-threaded, monolithic server design, Flux
  is built upon distributed message brokers and reactive (asynchronous)
  agents that communicate only with messages.  Building distributed services
  on this substrate is interesting, fun, and scales well.

Slurm Long Term Viability
=========================

When it was started at LLNL, Slurm was named "SLURM", a backronym for *Simple
Linux Utility for Resource Management*.  The guiding principle of its
design was simplicity, a laudable goal.  However, Slurm's brief design
phase and relatively short development period before its first production
deployment, followed by rapid expansion into even non-Linux environments,
put a lot of stress on that original design without leaving much time to
pay back the technical debt that accrued.

SchedMD and the impressive Slurm community have taken Slurm on quite a
journey since those days.  Despite growing complexity, the fundamental
design of Slurm has not changed and has not provided a strong basis for
the organic feature growth that has occurred.  Consequently, the Slurm
code base is not well positioned to support the emerging needs of
HPC/Cloud/ML computing into the future.

Continuing to extend Slurm indefinitely without breaking it will cost
more than building support for emerging capabilities on Flux, which
is well on its way to basic feature parity with Slurm and offers a
superior, stable foundation.

Command Equivalencies
=====================

LLNL's `Batch System Cross-Reference Guides
<https://hpc.llnl.gov/banks-jobs/running-jobs/batch-system-cross-reference-guides>`_ may be helpful.

Slurm Wrappers
==============

Asking very important people who do very important things to change their
workflows can cause friction.  Wrappers scripts that implement Slurm
functionality in terms of Flux are available if you need them.

Installing Slurm wrappers on a Flux system cuts two ways.  On one hand,
it can ease users through the transition to Flux and reduce the support
burden.  On the other hand, it can be a crutch that delays learning and
obfuscates problems.

.. list-table::
   :header-rows: 1

   * - Package
     - Functionality

   * - `flux-wrappers <https://github.com/LLNL/flux-wrappers>`_
     - Wrapper scripts to ease the transition to Flux.

