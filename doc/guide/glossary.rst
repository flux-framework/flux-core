Glossary
========

Here we define Flux-specific and general HPC and workload management terms
used in our documentation that may not be familiar to all readers.

.. glossary::

  enclosing instance
    The Flux instance that a process naturally interacts with.  It is
    the instance referred to by the :envvar:`FLUX_URI` environment variable,
    or if that is not set, it is the :term:`system instance`.

  evolving
    An evolving job is similar to a :term:`malleable` job, except that the
    application, rather than the system, may initiate resource grow and
    shrink at runtime [#Feitelson96]_.

  expedited
    A job is said to be expedited if its :term:`urgency` is set to the
    maximum value of 31.  An expedited job's :term:`priority` is always set
    to the maximum value.

  FSD
    A common string representation of time duration, defined by
    :doc:`rfc:spec_23`.  Example: ``2.5h``.

  guest
    A Flux user that is not the :term:`instance owner`.  Guests are only
    allowed to run in a Flux instance configured for multi-user support,
    normally a :term:`system instance`.

  held
    A job is said to be held if its :term:`urgency` is set to zero.  This
    prevents it from being considered for scheduling until the urgency is
    raised.

  hostlist
    A compact string representation of a list of hostnames, defined by
    :doc:`rfc:spec_29`.  Example: ``fluke[0-127,130]``.

  idset
    A compact string representation of a set of non-negative integers,
    defined by :doc:`rfc:spec_22`.  Example: ``2,4,6,1-100``.

  IMP
    The Independent Minister of Privilege.  The simple setuid root component
    of Flux, from the flux-security project, that allows an
    :term:`instance owner` to perform a limited set of tasks on behalf of a
    :term:`guest` user in a multi-user Flux instance.

  initial program
    A user-defined program, such as a batch script, launched on the first
    node of a Flux instance.  Its purpose is to launch and monitor a
    workload.  Once it is complete, the instance exits.

  instance owner
    The user that started the Flux instance.  The instance owner has control
    over all aspects of the Flux instance's operation.

  job
    The smallest unit of work that can be allocated resources and run by Flux.
    A job can be a Flux instance which in turn can run more jobs.

  jobspec
    The JSON or YAML object representing a Flux job request, defined by
    :doc:`rfc:spec_14` (the general specification) and :doc:`rfc:spec_25`
    (the current version).  It includes the abstract resource requirements
    of the job and instructions for job execution.

  malleable
    A malleable job requests a variable, bounded quantity of resources
    that the system may grow or shrink (within bounds) at runtime
    [#Feitelson96]_.

  moldable
    A moldable job requests a variable, bounded quantity of resources
    that, once allocated by the system, is fixed at runtime [#Feitelson96]_.

  priority
    The order in which the scheduler considers jobs.  By default, priority
    is derived from the :term:`urgency` and submit time, but a priority plugin
    can be used to override this calculation.

  R
    The JSON object used by Flux to represent a concrete resource set.
    See :doc:`rfc:spec_20`.

  resource inventory
    The concrete set of resources managed by a given Flux instance.

  rigid
    A rigid job requests a fixed quantity of resources that remains
    fixed at runtime [#Feitelson96]_.

  scheduler
    The Flux component that fulfills resource allocation requests from the
    :term:`resource inventory`.  Abstract resource requirements are extracted
    from the user-provided :term:`jobspec`, and fulfilled with a resource set
    expressed as :term:`R`. In addition to fitting concrete resources to
    abstract requests, the scheduler must balance goals such as fairness
    and resource utilization when it decides upon a schedule for fulfilling
    competing requests.

  slot
    The abstract resource requirements of one task.

  step
    In other workload managers, a job step is a unit of work within a job.
    Flux, which has a robust recursive definition of a :term:`job`, does not
    use this term.

  system instance
    A multi-user Flux instance running as the primary resource manager
    on a cluster.  The system instance typically runs as an unprivileged
    system user like ``flux``, is started by :linux:man1:`systemd`, and
    allows :term:`guest` users to run jobs.

  taskmap
    A compact mapping between job task ranks and node IDs, defined by
    :doc:`rfc:spec_34`.

  TBON
    Tree based overlay network.  Flux brokers are interconnected with one.

  urgency
    A job attribute that the user sets to indicate how urgent the work is.
    The range is 0 to 31, with a default value of 16.  Urgency is defined
    by :doc:`rfc:spec_30`.

Footnotes
---------

.. [#Feitelson96] Feitelson, D.G., Rudolph, L.: *Toward convergence in job
   scheduling for parallel supercomputers* (1996).
