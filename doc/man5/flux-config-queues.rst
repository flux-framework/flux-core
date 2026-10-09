=====================
flux-config-queues(5)
=====================


DESCRIPTION
===========

The ``queues`` table configures job queues, as described in RFC 33.
Normally, Flux has a single anonymous queue, but when queues are configured,
all queues are named, and a job submitted without a queue name is rejected
unless a default queue is configured.

Each key in the ``queues`` table is a queue name, whose value is a table
consisting of the following optional keys:

hosts (string)
   An RFC 29 hostlist naming the nodes that belong to the queue, or the
   literal string ``all`` to select every node.  Flux derives the queue's
   membership property from its name, prefixed with the RFC 20
   instance-local marker (``+NAME``), applies it to those nodes, and
   constrains jobs in the queue to them.  A host that is not among the
   instance's resources, or a hostlist that resolves to no nodes, is an
   error.  ``hosts`` and ``requires`` are mutually exclusive, and a virtual
   queue must not set ``hosts``.  Since the queue name becomes a property
   name, a queue that sets ``hosts`` must be named with a valid RFC 20
   property string of 126 characters or less.  ``hosts`` is the preferred
   way to define queue membership.

requires (array)
   A list of resource property names that selects a subset of the total
   configured resources for the queue.  Each property name is a string, as
   defined in RFC 20.  If multiple properties are listed, they are combined
   in a logical *and* operation.  Properties are attached to resources in the
   ``resource`` table as described in :man5:`flux-config-resource`.

policy (table)
   A policy table as described in :man5:`flux-config-policy` that overrides
   the general system policy for jobs submitted to this queue.

parent (string)
   Declares this queue to be a virtual queue, as described in RFC 33. The
   value names another queue in the ``[queues]`` table from which this
   queue inherits its resource subset and policy. A virtual queue MUST
   NOT set ``requires`` or ``policy.scheduler``: it always shares its
   parent's resources, and thus its parent's scheduler configuration.
   The named parent must not itself be a virtual queue. A virtual queue
   may be configured as the default queue.

A default queue name may be configured by setting
``policy.jobspec.defaults.system.queue`` as described in
:man5:`flux-config-policy`.


QUEUE MEMBERSHIP
================

``hosts`` is the preferred way to define a queue's membership.  It names
the queue's nodes directly.  Flux applies a derived ``+NAME`` property to
those nodes and constrains the queue's jobs to it.  As described in RFC
20, a ``+`` prefixed property is instance-local and is dropped from the
resource set assigned to a job, so it does not appear in a job's resources
set and therefore subinstances.

The property is derived when the ``resource`` module loads and is not
stored with the resources.  Use :command:`flux resource R` to view it;
it is not in the ``resource.R`` KVS key.  A change to ``hosts`` requires
a reload of the ``resource`` module to take effect.

``requires`` selects nodes by properties already assigned in the ``resource``
table, so the queue and its properties must be kept in agreement in two
places.  It remains the way to define a queue by an existing property, or
by more than one, and to share a property between queues.  Name the
property after the queue: :command:`flux resource list` hides a property
that matches a queue name, and shows one that does not.

A queue that sets neither key covers all resources.


VIRTUAL QUEUES
==============

A virtual queue is an alternate name under which jobs may be submitted to
a parent queue's resources, with different policy applied at job
ingest. A job submitted to a virtual queue keeps the virtual queue's name
for listing, accounting, and per-queue policy purposes, but is scheduled
as part of its parent queue: it competes for the parent's resources in the
same priority order as jobs submitted directly to the parent.

A virtual queue inherits every policy key from its parent, and any key it
sets itself overrides only that key. For example, a virtual queue that
sets ``policy.limits.duration`` but not ``policy.limits.job-size`` still
inherits the parent's job-size limits. Job defaults
(``policy.jobspec.defaults.system``) are inherited the same way.

A virtual queue may be enabled/disabled and started/stopped
independently of its parent. However, because a virtual queue's jobs are
scheduled as part of its parent, they are only eligible for scheduling
when both the virtual queue and its parent are started. Starting a
virtual queue whose parent is stopped does not release its jobs; they
become eligible only once the parent is also started.


EXAMPLE
=======

::

   [[resource.config]]
   hosts = "test[0-127]"
   cores = "0-1"

   [queues.debug]
   policy.limits.duration = "30m"
   hosts = "test[0-7]"

   [queues.batch]
   policy.limits.duration = "8h"
   policy.limits.job-size.max.nnodes = 16
   hosts = "test[8-127]"

   [queues.expedite]
   parent = "batch"
   policy.limits.duration = "1h"

   [policy.jobspec.defaults.system]
   queue = "batch"

   [sched-fluxion-qmanager]
   queue-policy-per-queue="batch:easy debug:fcfs"

   [sched-fluxion-resource]
   match-policy = "lonodex"
   match-format = "rv1_nosched"

In this configuration, ``expedite`` is a virtual queue of ``batch``: jobs
submitted with ``--queue=expedite`` run on ``batch``'s resources, subject
to a shorter 1 hour duration limit, but still inherit ``batch``'s
16 node job-size limit since ``expedite`` does not override it.


CAVEATS
=======

Queue resources should not overlap.


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_20`

:doc:`rfc:spec_33`


SEE ALSO
========

:man5:`flux-config`, :man5:`flux-config-policy`, :man5:`flux-config-resource`
