=====================
flux-config-policy(5)
=====================


DESCRIPTION
===========

The ``policy`` table captures general site preferences for job request defaults
and limits, as described in RFC 33.

Each queue defined in the ``queues`` table described in
:man5:`flux-config-queues` may have a ``policy`` sub-table that follows the
same rules as the main table.  The per-queue table overrides the general table
for jobs submitted to that queue.

DEFAULTS
========

Default values for job requests may be configured in the
``policy.jobspec.defaults.system`` table.  If a job request does not specify
a value for a system attribute that is configured in the table, the configured
value is substituted.  Some common examples are:

policy.jobspec.defaults.system.duration (float seconds or FSD string)
   (optional) If a job is submitted without specifying a job duration,
   this value is substituted.

policy.jobspec.defaults.system.queue (string)
   (optional) If a job is submitted without specifying a queue name,
   this value is substituted.

LIMITS
======

Limits may be configured in the ``policy.limits`` table.  Job requests are
rejected at submission time if they violate configured limits.

.. note::
   It is possible to set a queue-specific limit to an `unlimited` value,
   and thereby defeat a configured general limit for jobs submitted to that
   queue.  The actual value used to indicate `unlimited` varies by limit
   type and is noted below.

policy.limits.duration (float seconds or FSD string)
   (optional) Maximum duration that a job may request (0 = unlimited).

.. note::
   Limit checks take place before the scheduler sees the request, so it is
   possible to bypass a node limit by requesting only cores, or the core limit
   by requesting only nodes (exclusively) since this part of the system does
   not have detailed resource information.  Generally node and core limits
   should be configured in tandem to be effective on resource sets with
   uniform cores per node.  Flux does not yet have a solution for node/core
   limits on heterogeneous resources.

policy.limits.job-size.max.nnodes (integer)
   (optional) Maximum number of nodes that may be requested (-1 = unlimited).

policy.limits.job-size.max.ncores (integer)
   (optional) Maximum number of cores that may be requested (-1 = unlimited).

policy.limits.job-size.max.ngpus (integer)
   (optional) Maximum number of GPUs that may be requested (-1 = unlimited).

policy.limits.job-size.min.nnodes (integer)
   (optional) Minimum number of nodes that may be requested (-1 = unlimited).

policy.limits.job-size.min.ncores (integer)
   (optional) Minimum number of cores that may be requested (-1 = unlimited).

policy.limits.job-size.min.ngpus (integer)
   (optional) Minimum number of GPUs that may be requested (-1 = unlimited).



EXAMPLE
=======

::

   [policy.defaults]
   duration = "1h"
   queue = "pbatch"

   [policy.limits]
   duration = "4h"
   job-size.max.nnodes = 8
   job-size.max.gpus = 4

   [queues.pdebug.policy.limits]
   duration = "30m"
   job-size.max.gpus = -1  # unlimited


RESOURCES
=========

Flux: http://flux-framework.org

RFC 23: Flux Standard Duration: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_23.html

RFC 33: Flux Job Queues: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_33.html

SEE ALSO
========

:man5:`flux-config`
