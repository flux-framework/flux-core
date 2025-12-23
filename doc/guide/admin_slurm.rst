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

The Flux project began around 2012.  Flux has been used for a decade or more
for managing complex ensembles and workflows at LLNL under Slurm and LSF
in situations where the traditional workload managers were not up to the task.
Although Flux was designed from the beginning to replace Slurm and was
deployed as such on several small systems at LLNL, it did not gain momentum
as a system workload manager until 2024 with the early deliveries of
`El Capitan <https://hpc.llnl.gov/hardware/compute-platforms/el-capitan>`_.

Flux is now in daily production use as the sole system workload manager on
El Capitan (currently, in late 2025, in slot 1 of the `TOP500 <https://www.top500.org/>`_)
and its unclassified sister systems at LLNL.  These machines are capability
workhorses, continuously in demand for LLNL's most cutting edge,
mission-critical activities.  The process of standing them up brought the
Flux team together to address problems of scale and stability with
unprecedented urgency.  As a result, system deployments of Flux on
virtually any size system are viable at this point.

This experience brought missing features into focus that have now been
prioritized for near term development.  We expect substantial progress to
be made on all of these items in 2026:

Rolling software upgrade
  Flux nodes won't interact with other Flux nodes unless they are running
  *exactly* the same flux-core version.  When flux-core 1.0.0 is released,
  relaxed rules will be enacted to support rolling upgrades.

Flux restart with running jobs
  Restarting Flux kills running jobs.  The design to allow running
  jobs to continue is not yet fully implemented.

Reservations
  Flux does not yet have a way to request immovable future allocations.
  Arrangements for dedicated application time currently
  require manual/scripted actions by system administrators.

Flux graph-based scheduling scalability
  Efforts to optimize the Fluxion scheduler graph-based resource
  representation for storage space efficiency are ongoing.  Meanwhile a
  flat resource model similar to Slurm's that scales well but is limited
  in capability is typically used on large systems.

Preservation of job step information
  Slurm's smallest unit of work is the job step.  Slurm keeps metadata
  for each step which can be retrieved after the job (allocation) completes.
  In Flux, there are no job steps.  Instead, a sub-instance of Flux is started
  on a resource allocation, and user applications are run as jobs within the
  sub-instance.  There is little coupling between the system instance and its
  sub-instances which improves scalability.  Unfortunately, sub-instance job
  data is not preserved unless the user explicitly arranges for it, which some
  find surprising.

Multi-system fair share accounting
  Flux has an optional fair-share accounting system for gathering usage data
  and setting job priorities.  Unlike Slurm, there is not yet a capability to
  deploy one accounting system for multiple Flux systems.

Package availability
  Flux system administrators are encouraged to install Flux as system packages,
  but pre-built packages are not widely available.  Source RPM packages for
  RHEL 8 and 9 are manually attached to GitHub releases.  Very soon this will
  be automated for all sub-projects that the team currently packages for RHEL.
  Also, discussions are underway with Red Hat regarding inclusion in Fedora
  and EPEL.

Commercial support and training
  This is an important and current area of discussion.

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

