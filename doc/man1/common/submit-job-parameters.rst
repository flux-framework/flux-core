JOB PARAMETERS
==============

These commands accept only the simplest parameters for expressing
the size of the parallel program and the geometry of its task slots:

Common resource options
-----------------------

These commands take the following common resource allocation options:

.. option:: -N, --nodes=N

   Set the number of nodes to assign to the job. Tasks will be distributed
   evenly across the allocated nodes, unless the per-resource options
   (noted below) are used with *submit*, *run*, or *bulksubmit*. It is
   an error to request more nodes than there are tasks. If unspecified,
   the number of nodes will be chosen by the scheduler.

.. option:: -x, --exclusive

   Indicate to the scheduler that nodes should be exclusively allocated to
   this job. It is an error to specify this option without also using
   :option:`--nodes`. If :option:`--nodes` is specified without
   :option:`--nslots` or :option:`--ntasks`, then this option will be enabled
   by default and the number of tasks or slots will be set to the number of
   requested nodes.


Per-task options
----------------

:man1:`flux-run`, :man1:`flux-submit` and :man1:`flux-bulksubmit` take two
sets of mutually exclusive options to specify the size of the job request.
The most common form uses the total number of tasks to run along with
the amount of resources required per task to specify the resources for
the entire job:

.. option:: -n, --ntasks=N

   Set the number of tasks to launch (default 1).

.. option:: -c, --cores-per-task=N

   Set the number of cores to assign to each task (default 1).

.. option:: -g, --gpus-per-task=N

   Set the number of GPU devices to assign to each task (default none).

Per-resource options
--------------------

The second set of options allows an amount of resources to be specified
with the number of tasks per core or node set on the command line. It is
an error to specify any of these options when using any per-task option
listed above:

.. option:: --cores=N

   Set the total number of cores.

.. option:: --tasks-per-node=N

   Set the number of tasks per node to run.

.. option:: --gpus-per-node=N

   With :option:`--nodes`, request a specific number of GPUs per node.

.. option:: --tasks-per-core=N

   Force a number of tasks per core. Note that this will run *N* tasks per
   *allocated* core. If nodes are exclusively scheduled by configuration or
   use of the :option:`--exclusive` flag, then this option could result in many
   more tasks than expected. The default for this option is effectively 1,
   so it is useful only for oversubscribing tasks to cores for testing
   purposes. You probably don't want to use this option.

Batch job options
-----------------

:man1:`flux-batch` and :man1:`flux-alloc` do not launch tasks directly, and
therefore job parameters are specified in terms of resource slot size
and number of slots. A resource slot can be thought of as the minimal
resources required for a virtual task. The default slot size is 1 core.

.. option:: -n, --nslots=N

   Set the number of slots requested. This parameter is required.

.. option:: -c, --cores-per-slot=N

   Set the number of cores to assign to each slot (default 1).

.. option:: -g, --gpus-per-slot=N

   Set the number of GPU devices to assign to each slot (default none).

Additional job options
----------------------

These commands also take following job parameters:

.. option:: -q, --queue=NAME

   Submit a job to a specific named queue. If a queue is not specified
   and queues are configured, then the jobspec will be modified at ingest
   to specify the default queue. If queues are not configured, then this
   option is ignored, though :man1:`flux-jobs` may display the queue
   name in its rendering of the ``{queue}`` attribute.

.. option:: -t, --time-limit=MINUTES|FSD

   Set a time limit for the job in either minutes or Flux standard duration
   (RFC 23). FSD is a floating point number with a single character units
   suffix ("s", "m", "h", or "d"). The default unit for the
   :option:`--time-limit` option is minutes when no units are otherwise
   specified. If the time limit is unspecified, the job is subject to the
   system default time limit.

.. option:: --job-name=NAME

   Set an alternate job name for the job.  If not specified, the job name
   will default to the command or script executed for the job.

.. option:: --flags=FLAGS

   Set comma separated list of job submission flags.  The possible flags are
   ``waitable``, ``novalidate``, and ``debug``.  The ``waitable`` flag will
   allow the job to be waited on via ``flux job wait`` and similar API calls.
   The ``novalidate`` flag will inform flux to skip validation of a job's
   specification.  This may be useful for high throughput ingest of a large
   number of jobs.  Both ``waitable`` and ``novalidate`` require instance
   owner privileges.  ``debug`` will output additional debugging into the job
   eventlog.

