.. _python_scheduler:

.. currentmodule:: flux.scheduler

Writing a Python Scheduler
==========================

Flux supports scheduler broker modules written in Python.  The
:class:`Scheduler` base class handles all of the RFC 27
protocol scaffolding — service registration, resource acquisition, the
hello/ready handshake with job-manager — so that a Python scheduler only
needs to implement scheduling policy.

This guide walks through writing a working scheduler from scratch,
explains each override point, and covers more advanced topics such as
job annotations and start-time forecasting.


Minimal example
---------------

The base class maintains a :class:`PendingJob` heap queue
(:attr:`~Scheduler._queue`) and provides default
implementations of :meth:`~Scheduler.alloc`,
:meth:`~Scheduler.free`,
:meth:`~Scheduler.cancel`, and
:meth:`~Scheduler.prioritize`.  A minimal scheduler only
needs to override :meth:`~Scheduler.schedule`:

.. code-block:: python

   import heapq
   from flux.resource import InsufficientResources, InfeasibleRequest
   from flux.scheduler import Scheduler

   class SimpleScheduler(Scheduler):

       def schedule(self):
           while self._queue:
               job = self._queue[0]
               try:
                   alloc = self.resources.alloc(job.jobid, job.resource_request)
               except InsufficientResources:
                   break   # not enough resources — wait for free
               except InfeasibleRequest as exc:
                   job.request.deny(str(exc))
               else:
                   job.request.success(alloc)
               heapq.heappop(self._queue)

   def mod_main(h, *args):
       SimpleScheduler(h, *args).run()

Save this as :file:`my-sched.py` and load it:

.. code-block:: console

   $ flux module unload sched-simple
   $ flux module load my-sched.py

The :func:`mod_main` entry point is the same convention used by all Python
broker modules (see :ref:`python_broker_modules`).


The resource pool
-----------------

:attr:`~Scheduler.resources` is an instance of
:class:`~flux.resource.Rv1Pool` that represents the entire managed resource
set.  It tracks which resources are up, down, and currently allocated.
The pool implementation is selected automatically based on the R version
field in the resource data (currently only version 1 is supported).

The resource request
~~~~~~~~~~~~~~~~~~~~

Each :class:`PendingJob` carries a pool-specific resource request object in
``job.resource_request``, parsed once from the jobspec when the alloc request
arrives by :meth:`~flux.resource.ResourcePool.parse_resource_request`.  The
type and fields of this object are defined by the pool; scheduler policy code
treats it as opaque and passes it directly to
:meth:`~flux.resource.ResourcePool.alloc`.

If you need the raw jobspec dict — for instance, to inspect job attributes,
it is passed as the *jobspec* parameter to :meth:`~Scheduler.alloc`:

.. code-block:: python

   def alloc(self, request, jobid, priority, userid, t_submit, jobspec):
       # jobspec is the parsed dict; inspect it before calling super()
       super().alloc(request, jobid, priority, userid, t_submit, jobspec)

Allocating resources
~~~~~~~~~~~~~~~~~~~~

Pass the jobid and resource request to
:meth:`~flux.resource.ResourcePool.alloc`:

.. code-block:: python

   try:
       alloc = self.resources.alloc(job.jobid, job.resource_request)
   except InsufficientResources:
       # Not enough resources now — queue the request and retry later
       ...
   except OSError as exc:
       # Request can never be satisfied (InfeasibleRequest or other error)
       request.deny(str(exc))
       return

RFC 31 constraint expressions (``constraint`` field on the resource request)
are carried through automatically; the resource pool evaluates them when
selecting candidate nodes.

On success, :meth:`~flux.resource.ResourcePool.alloc` returns a resource
pool object containing only the allocated resources.  Pass it directly to
:meth:`~AllocRequest.success`, which converts it automatically:

.. code-block:: python

   request.success(alloc)

The pool also records the job's expected expiration (derived from
``request.duration``) internally; :meth:`~flux.resource.ResourcePool.free`
and start-time simulation (via :meth:`~flux.resource.ResourcePool.copy`
and :meth:`~flux.resource.ResourcePool.job_end_times`) use this state.

Releasing resources
~~~~~~~~~~~~~~~~~~~

The base class :meth:`~Scheduler.free` calls
:meth:`~flux.resource.ResourcePool.free` automatically — no override is
needed unless the scheduler has additional per-job cleanup to do.

A job's resources may be released in multiple calls when housekeeping is
configured: housekeeping returns resources in batches of one or more ranks as
they become ready, with *final* set to ``False`` until the last batch.
Schedulers that override :meth:`~Scheduler.free` must handle this: *R*
contains only the subset being freed on each call, and per-job state should
not be discarded until *final* is ``True``.

Resource state changes
~~~~~~~~~~~~~~~~~~~~~~

Node drain/undrain and other resource state changes arrive automatically
via the ``resource.acquire`` streaming RPC.  After each update, the base
class calls :meth:`~Scheduler.resource_update`, giving the
scheduler a chance to retry pending allocations:

.. code-block:: python

   def resource_update(self):
       self._retry_pending()


Alloc requests
--------------

When job-manager asks the scheduler to allocate resources for a job, two
distinct objects are created and stored together on the
:class:`PendingJob`:

- ``job.resource_request`` — a pool-specific object describing *what the job
  needs*.  Created once from jobspec by
  :meth:`~flux.resource.ResourcePool.parse_resource_request` when the alloc
  arrives.  For the built-in pools this carries fields for node count,
  slot count, cores and GPUs per slot, duration, constraints, and exclusivity.
  The raw jobspec dict is also available as ``job.resource_request.jobspec``,
  which is useful for reading site-specific hints from
  ``attributes.system`` that the standard parser does not extract —
  for example, ``job.resource_request.jobspec.get("attributes",
  {}).get("system", {}).get("rack_exclusive", False)``.
  Used by the scheduler during each :meth:`~Scheduler.schedule`
  pass to decide whether resources can be satisfied.  Persists for the
  lifetime of the pending job.

- :class:`AllocRequest` (``job.request``) — represents the
  *open RFC 27 protocol transaction* with job-manager.  Wraps the message
  handle and provides the response methods that finalize the exchange.  Must
  be finalized exactly once; consumed when the scheduler responds.

The scheduler reads :attr:`~PendingJob.resource_request` to
make scheduling decisions, then calls a method on
:attr:`~PendingJob.request` to report the outcome.

:class:`AllocRequest` must be finalized exactly once by
calling one of:

:meth:`request.success(R, annotations=None) <AllocRequest.success>`
    Allocation succeeded.  *R* is the pool object returned by
    :meth:`~flux.resource.ResourcePool.alloc`; it is converted to a dict
    automatically.  The base class commits R to the KVS asynchronously
    before sending the response to job-manager, ensuring R is safely
    stored before the job can run.

:meth:`request.deny(note=None) <AllocRequest.deny>`
    Permanent denial — the job will never run.  *note* is a human-readable
    reason that appears in :man1:`flux-job` eventlog.  Use this for
    structurally unsatisfiable requests (too many cores, unsupported resource
    type, etc.).

:meth:`request.cancel() <AllocRequest.cancel>`
    The alloc request is being withdrawn.  Call this from
    :meth:`~Scheduler.cancel`.

    .. note::

       Cancelling an alloc request is **not** the same as cancelling the job.
       Job-manager may withdraw an alloc request — for example, to shrink the
       outstanding request count back within the configured ``queue-depth`` —
       while leaving the job itself in the pending queue.  The job
       will receive a fresh alloc request when a slot opens up.

:meth:`request.annotate(annotations) <AllocRequest.annotate>`
    Send an intermediate annotation update while the job is pending.  May be
    called any number of times before finalizing.  Annotations are visible
    in :man1:`flux-jobs` output.

    RFC 27 defines two standard ``sched`` annotation keys:

    resource_summary
        Free-form string describing the allocated resources
        (e.g., ``"rank[0-1]/core[0-3]"``).  Set this on a successful
        allocation.

    t_estimate
        Estimated start time as a Unix epoch float, or ``null`` to clear.
        Set this on pending jobs that have a shadow reservation; clear it
        on allocation.

    Example (planning scheduler posting a reservation estimate):

    .. code-block:: python

       # RFC 27 sched annotation: t_estimate is wall clock seconds since epoch
       request.annotate({"sched": {"t_estimate": shadow_time}})

    When a pending job is cancelled the base class automatically clears any
    annotations that were previously sent for that job — no explicit null
    annotation is needed in :meth:`~Scheduler.cancel` overrides.

    .. note::

       Scheduler annotations are **not** recorded in the job eventlog.
       :man1:`flux-job` ``wait-event annotations`` cannot be used to wait for
       them in tests; instead poll :man1:`flux-jobs`
       ``-no {annotations.sched.field}``.


Override points
---------------

Subclass :class:`Scheduler` and override any of the
following methods:

:meth:`hello(self, jobid, priority, userid, t_submit, R) <Scheduler.hello>`
    Called once per running job during startup, before the reactor starts.
    The default marks *R* as allocated in the resource pool.  Override to
    also record per-job state needed by the scheduler subclass.

:meth:`alloc(self, request, jobid, priority, userid, t_submit, jobspec) <Scheduler.alloc>`
    Called when job-manager requests an allocation.  The default calls
    :meth:`~flux.resource.ResourcePool.parse_resource_request` on the pool
    to parse *jobspec* and pushes a :class:`PendingJob` onto
    :attr:`_queue`.  Override to reject jobs before they enter the queue.

:meth:`free(self, jobid, R, final=False) <Scheduler.free>`
    Called when resources are released for a completed or cancelled job.
    *final* is ``True`` on the last (or only) call for the job.  When
    housekeeping is configured, resources may arrive in multiple partial
    batches before *final* is set; see `Releasing resources`_ for details.
    The default calls :meth:`~flux.resource.ResourcePool.free` on the pool.
    Override (calling :meth:`~Scheduler.free` via ``super()``) to also
    remove per-job state or log the event, taking care not to discard
    per-job state until *final* is ``True``.

:meth:`cancel(self, jobid) <Scheduler.cancel>`
    Called when a pending alloc is cancelled by the user.  The default
    removes *jobid* from :attr:`~Scheduler._queue` and calls
    :meth:`~AllocRequest.cancel`.

:meth:`prioritize(self, jobs) <Scheduler.prioritize>`
    Called with a list of ``[jobid, priority]`` pairs when job priorities
    change.  The default updates the priority of each job in
    :attr:`~Scheduler._queue` and re-heapifies.

:meth:`schedule(self) <Scheduler.schedule>`
    Called to run the scheduling loop after one or more scheduling events
    (alloc, free, cancel, prioritize, expiration, or resource state change).
    Events are coalesced by an adaptive timer so that
    :meth:`~Scheduler.schedule` is not called more often than
    necessary; see `Scheduling deferral`_ for details.
    The default implementation is a no-op.

    If :meth:`~Scheduler.schedule` returns a generator the base class
    advances it **one yield per reactor iteration**, allowing other events
    to be handled between yields.
    When a new scheduling event arrives while a generator pass is in progress
    the base class closes it and starts a fresh pass after the settling delay,
    ensuring that a newly submitted job or freed resource is considered from
    the top of the queue without waiting for the current pass to finish.
    Non-generator implementations remain fully supported.

    Example::

        def schedule(self):
            while self._queue:
                job = self._queue[0]
                try:
                    alloc = self.resources.alloc(job.jobid, job.resource_request)
                except InsufficientResources:
                    break   # head blocked; stop (FIFO)
                except OSError as exc:
                    job.request.deny(str(exc))
                else:
                    job.request.success(alloc)
                heapq.heappop(self._queue)
                yield   # let the reactor handle other events between jobs

:meth:`start_schedule(self) <Scheduler.start_schedule>`
    Called by :meth:`~Scheduler._request_schedule` after updating the
    interval EWMA.  The default implementation aborts any in-progress
    generator and arms the one-shot scheduling timer.  Override this
    method (not :meth:`~Scheduler._request_schedule`) to replace the
    generator driver — for example, to launch an RPC-based allocation
    request — while still preserving the ``_sched_pending`` guard that
    coalesces concurrent scheduling events into a single pass.

:meth:`resource_update(self) <Scheduler.resource_update>`
    Called after each resource state update.  For queue-based schedulers
    that rely on :meth:`~Scheduler.schedule`, this override
    is usually not needed because the base class calls
    :meth:`~Scheduler._request_schedule` automatically after
    every resource update.  Override only if you need to react to the updated
    :attr:`~Scheduler.resources` value before the scheduling
    pass.

:meth:`feasibility_check(self, msg, jobspec) <Scheduler.feasibility_check>`
    Called for ``feasibility.check`` RPCs from job-ingest.  The default
    calls :meth:`~flux.resource.ResourcePool.parse_resource_request` and
    :meth:`~flux.resource.ResourcePool.check_feasibility` on the pool,
    responding with ``EINVAL`` if the job can never fit the total resource
    set.  Override only if custom feasibility logic is needed.

:meth:`forecast(self) <Scheduler.forecast>`
    Called after each :meth:`~Scheduler.schedule` pass to annotate pending
    jobs with forward-looking estimates such as ``sched.t_estimate``.  The
    base class implementation is a no-op.  Override to post start-time
    estimates (or other planning metadata) without impacting scheduling
    throughput.

    Supports the same generator protocol as :meth:`~Scheduler.schedule`:
    add ``yield`` at each desired reactor handoff point — typically after each
    annotated job — to return control to the reactor between annotations.
    Unlike the schedule generator, a running forecast generator is **not**
    aborted when a new scheduling event arrives — it runs to completion.
    A slightly stale ``t_estimate`` is more useful than none at all, and
    the simulation snapshot taken at pass start remains internally
    consistent regardless of real-pool changes mid-pass.

    Example (annotating the head-of-queue job with its estimated start time):

    .. code-block:: python

       def forecast(self):
           if not self._queue:
               return
           head = sorted(self._queue)[0]
           t = self._shadow_time(head)   # simulate pool forward to find earliest start
           if head._last_annotation != t:
               head._last_annotation = t
               head.request.annotate({"sched": {"t_estimate": t}})


Queue depth
-----------

The ``queue-depth=`` module argument controls how many alloc requests
job-manager sends concurrently:

- ``queue-depth=unlimited`` (the default) — all pending jobs get an alloc
  request at once.  The scheduler sees the full queue on startup.

- ``queue-depth=N`` (a positive integer) — job-manager sends at most N
  outstanding requests.  Use this to bound the scheduler queue and annotation
  overhead.  :class:`FIFOScheduler` defaults to ``queue-depth=8`` to match
  the behaviour of the built-in ``sched-simple``.

Set the class attribute :attr:`~Scheduler.queue_depth` on the
subclass to choose a default; use a plain integer or the string
``"unlimited"``:

.. code-block:: python

   class MyScheduler(Scheduler):
       queue_depth = 8   # or "unlimited"

The base class translates the value to the wire format for the hello
protocol automatically.  Users can override the default at load time:

.. code-block:: console

   $ flux module load my-sched.py queue-depth=unlimited


Scheduling deferral
-------------------

Every scheduling event (alloc, free, cancel, prioritize, expiration, resource
update) calls :meth:`~Scheduler._request_schedule` rather than
calling :meth:`~Scheduler.schedule` directly.  The base class
defers the actual call via a one-shot timer, coalescing multiple events into a
single :meth:`~Scheduler.schedule` invocation.

The timer uses an adaptive delay tuned automatically at runtime:

- The delay **starts at zero**, so the first event after an idle period is
  processed on the very next reactor iteration — no added latency.
- After each :meth:`~Scheduler.schedule` call, the base class
  compares two exponential moving averages (EWMAs) [#ewma]_:

  - :attr:`~Scheduler._sched_interval_ewma` — average time
    between :meth:`~Scheduler._request_schedule` calls
    (measures how fast events are arriving).
  - :attr:`~Scheduler._sched_duration_ewma` — average time
    :meth:`~Scheduler.schedule` takes to run (measures
    scheduling cost).

- If events arrive **faster than** :meth:`~Scheduler.schedule`
  can process them (``interval < duration``), the timer delay is raised to
  approximately ``duration``, coalescing the burst into roughly one scheduling
  pass per cycle.  A ``DEBUG`` log message is emitted when the delay changes.
- Once events slow down again the delay resets to zero immediately.

.. note::

   When :meth:`~Scheduler.schedule` is a generator the EWMA duration is
   **not** updated, so ``sched_delay`` remains 0 and the timer fires on
   the next reactor iteration with no burst-coalescing window.  Events
   that arrive while the timer is already armed are still coalesced by
   the ``_sched_pending`` guard, but there is no adaptive settling period.

Two class attributes control the behaviour and can be overridden on the
subclass:

.. code-block:: python

   class MyScheduler(Scheduler):
       SCHED_DELAY_MAX  = 1.0   # seconds; cap on adaptive delay (default 1s)
       SCHED_EWMA_ALPHA = 0.25  # EWMA smoothing factor (default 0.25 ≈ 4 samples)

:attr:`~Scheduler.SCHED_DELAY_MAX` bounds worst-case scheduling
latency during a sustained burst.
:attr:`~Scheduler.SCHED_EWMA_ALPHA` controls how quickly the
timer reacts: higher values respond faster but are noisier; lower values are
smoother but adapt more slowly.

For schedulers with a small ``queue-depth=`` (e.g. ``sched-fifo``'s default
of 8), events arrive at most 8-at-a-time so the interval rarely falls below
the schedule duration and the delay stays near zero.  Schedulers using
``queue-depth=unlimited`` benefit most from the adaptive timer during large
submission bursts.

.. rubric:: Footnotes

.. [#ewma] An *exponential moving average* (EMA, or EWMA with weights) is a
   low-pass filter that gives exponentially decreasing weight to older
   samples: ``new = α × sample + (1 − α) × old``.  With ``α = 0.25`` the
   influence of a sample halves roughly every three updates.  See
   https://en.wikipedia.org/wiki/Exponential_smoothing for background.


Forecast deferral
-----------------

:meth:`~Scheduler.forecast` is called immediately after each
:meth:`~Scheduler.schedule` pass completes.  Because
:meth:`~Scheduler.forecast` supports the same generator protocol as
:meth:`~Scheduler.schedule`, annotation work may be spread across multiple
reactor iterations to avoid blocking the critical scheduling path.

If a new scheduling event arrives while a forecast generator is in progress,
the base class leaves it running to completion.  Forecast estimates are
approximate by design, and a slightly stale ``t_estimate`` is more useful
than none at all.  The simulation snapshot is taken once at pass start
(a deep copy of the pool), so it remains internally consistent regardless
of real-pool changes mid-pass.  A fresh forecast pass is triggered after
the next :meth:`~Scheduler.schedule` pass completes.


Module arguments
----------------

Arguments passed after the module path at load time arrive as ``*args``
in :func:`mod_main` and are forwarded to ``__init__``:

.. code-block:: console

   $ flux module load my-sched.py queue-depth=8 log-level=debug

The base class automatically handles four built-in arguments:

queue-depth=N|unlimited
    Maximum number of concurrent outstanding alloc requests (default 8, or
    ``"unlimited"`` if :attr:`~Scheduler.queue_depth` is set to that string
    on the subclass).

log-level=LEVEL
    Minimum log severity to emit.  *LEVEL* is one of ``emerg``, ``alert``,
    ``crit``, ``err``, ``warning``, ``notice``, ``info``, or ``debug``
    (default ``info``).

pool-class=URI
    Select a custom resource pool class at load time.  *URI* is resolved
    by :meth:`~Scheduler._pool_class_from_uri`: a ``file://`` URI loads the
    named Python file and uses the class whose name matches the file stem
    (or an explicit ``#fragment``); other URI schemes import a Python module
    and read its ``pool_class`` attribute.  Equivalent to setting
    :attr:`~Scheduler.pool_class` on the subclass, but applied at load time
    without subclassing.  See `Pool class hook (pool_class)`_.

Any argument not consumed by the subclass or the base class is rejected
with an error at load time, so a typo like ``log_level=debug`` (underscore
instead of hyphen) is caught immediately.  Subclasses parse their own
arguments before calling ``super().__init__``, which consumes the built-in
ones and then rejects whatever remains:

.. code-block:: python

   def __init__(self, h, *args):
       # Parse custom arguments first; leave the rest for the base class.
       remaining = []
       for arg in args:
           if arg.startswith("my-option="):
               self._my_option = arg[10:]
           else:
               remaining.append(arg)
       super().__init__(h, *remaining)   # handles queue-depth=, log-level=



Logging
-------

Both the scheduler and pool implementations log via ``self.log(level, msg)``,
where *level* is a :mod:`syslog` priority constant.  The base class filters
messages against :attr:`~Scheduler.log_level` before forwarding to
``handle.log``, so only messages at or above the configured severity are
emitted:

.. code-block:: python

   import syslog
   self.log(syslog.LOG_DEBUG, f"alloc: {jobid}: {alloc.dumps()}")
   self.log(syslog.LOG_ERR,   f"unexpected error: {exc}")

The default :attr:`~Scheduler.log_level` is ``LOG_INFO``.  Set it at load
time to enable more verbose output:

.. code-block:: console

   $ flux module load my-sched.py log-level=debug

Valid level names are ``emerg``, ``alert``, ``crit``, ``err``, ``warning``,
``notice``, ``info``, and ``debug``.

Pool implementations receive the same ``self.log`` method and should call it
unconditionally — no ``None`` check is needed.

Log messages appear in :man1:`flux-dmesg` and the broker's stderr.


Statistics
----------

The base class registers a ``<module-name>.stats-get`` RPC handler that
calls :meth:`~Scheduler.stats_get` and responds with the returned dict.
Use :man1:`flux-module` to query it:

.. code-block:: console

   $ flux module stats my-sched

Standard fields reported by the base class:

``sched_passes``
    Number of completed :meth:`~Scheduler.schedule` passes.
``sched_yields``
    Total yields across all :meth:`~Scheduler.schedule` generator passes
    (always 0 for synchronous schedulers).
``forecast_passes``, ``forecast_yields``
    Equivalent counters for :meth:`~Scheduler.forecast` passes
    (``forecast_yields`` always 0 for synchronous schedulers).
``sched_delay``
    Current adaptive burst-coalescing delay in seconds.
    Always 0 for generator-based schedulers.
``sched_duration_ewma``
    EWMA of :meth:`~Scheduler.schedule` wall-clock duration in seconds.
    Always 0 for generator-based schedulers.
``sched_interval_ewma``
    EWMA of time between scheduling requests in seconds.  Tracked for all
    schedulers but does not affect ``sched_delay`` for generator-based
    schedulers.
``pending_jobs``
    Current number of pending alloc requests in the scheduler queue.

Subclasses can extend the response by overriding :meth:`~Scheduler.stats_get`:

.. code-block:: python

   def stats_get(self):
       stats = super().stats_get()
       stats["my_counter"] = self._my_counter
       return stats


Testing
-------

Use :man1:`flux-module` to load and remove the scheduler during a running
instance:

.. code-block:: console

   $ flux module unload sched-simple
   $ flux module load ./my-sched.py
   $ flux run -n2 hostname
   $ flux module reload my-sched.py          # reload without restarting instance
   $ flux module remove my-sched.py
   $ flux module load sched-simple

Write sharness shell tests using ``test_under_flux`` with the ``job``
personality (which loads a mock resource set and disables job execution),
replacing the default scheduler with your own:

.. code-block:: sh

   test_under_flux 4 job

   test_expect_success 'load my scheduler' '
       flux module unload sched-simple &&
       flux module load ./my-sched.py
   '
   test_expect_success 'job runs' '
       jobid=$(flux submit hostname) &&
       flux job wait-event --timeout=5 $jobid alloc
   '

See :file:`t/t2306-sched-fifo.t` in the source tree for a comprehensive
example covering annotations, priority, cancel, drain/undrain, and the
hello/reload protocol.


Customizing the resource pool
-----------------------------

The built-in :class:`~flux.resource.Rv1Pool` pool implementation exposes
two override points that let a scheduler enforce topology-aware allocation
policies without replacing the entire pool.  A third hook lets the scheduler
inject a custom pool class without overriding
:meth:`~Scheduler._acquire_resources`.

Pool-level selection hook
~~~~~~~~~~~~~~~~~~~~~~~~~

Override :meth:`~flux.resource.Rv1Pool.Rv1Pool._select_resources` to control
which candidates are chosen during each allocation.  The base class calls
this method with a worst-fit-sorted list of
``(rank, info, free_cores, free_gpus)`` tuples that already pass
availability and constraint filters:

.. code-block:: python

   from flux.resource import InsufficientResources
   from flux.resource.Rv1Pool import Rv1Pool

   class _RackPoolV1(Rv1Pool):

       def _select_resources(self, candidates, request):
           # Group candidates by rack, then try the fullest rack first.
           racks = {}
           for entry in candidates:
               rack_id = self._rack_map.get(entry[0])
               if rack_id is not None:
                   racks.setdefault(rack_id, []).append(entry)
           for _count, rack_candidates in sorted(
               ((len(v), v) for v in racks.values()), reverse=True
           ):
               try:
                   return super()._select_resources(rack_candidates, request)
               except InsufficientResources:
                   continue
           raise InsufficientResources("no single rack has sufficient resources")

The method must return ``(selected, actual_nslots)`` on success, or raise
:exc:`~flux.resource.InsufficientResources` if the candidates cannot satisfy
the request.  Calling ``super()._select_resources(candidates, request)``
delegates to the base greedy loop for the candidate subset.

Pool-level feasibility hook
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Override :meth:`~flux.resource.Rv1Pool.Rv1Pool._check_feasibility` to add
topology-aware permanent-denial logic.  The base class call must come first
so that basic structural checks (node count, cores, GPUs, constraints) are
enforced before the topology check:

.. code-block:: python

   from flux.resource import InfeasibleRequest
   from flux.resource.Rv1Pool import Rv1Pool

   class _RackPoolV1(Rv1Pool):

       def _check_feasibility(self, request):
           super()._check_feasibility(request)   # base structural checks first

           if not self._rack_map or request.nnodes == 0:
               return
           max_rack_nodes = max(
               sum(1 for r in self._ranks if self._rack_map.get(r) == rid)
               for rid in set(self._rack_map.values())
           )
           if max_rack_nodes < request.nnodes:
               raise InfeasibleRequest(
                   f"rack-local request for {request.nnodes} node(s) cannot be "
                   f"satisfied: largest rack has {max_rack_nodes} node(s)"
               )

Raising :exc:`~flux.resource.InfeasibleRequest` causes the scheduler to
permanently deny the job via :meth:`~AllocRequest.deny`.  Returning
normally indicates the request is structurally satisfiable; the scheduler
will keep it pending and retry allocation when resources become available.

Both hooks receive the full :class:`~flux.resource.Rv1Pool.ResourceRequest`
object — the same object stored as ``job.resource_request`` on the
:class:`PendingJob`.  This includes ``request.jobspec`` for reading
site-specific ``attributes.system`` hints beyond the standard parsed fields.

Pool class hook (pool_class)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are three ways to bind a custom pool class to a scheduler:

1. **Class attribute** — set :attr:`~Scheduler.pool_class` on the subclass:

   .. code-block:: python

      from flux.scheduler import Scheduler

      class RackScheduler(Scheduler):
          pool_class = RackPool
          # must also override schedule() to allocate jobs

2. **Module argument** — pass ``pool-class=URI`` at load time (no subclassing
   required):

   .. code-block:: console

      $ flux module load sched-simple pool-class=file:///path/to/RackPool.py

3. **Writer auto-discovery** — if the system R carries a ``scheduling.writer``
   URI pointing to a pool implementation, the scheduler loads it automatically
   without any explicit argument.  See `Writer identification`_.

In all three cases :attr:`~Scheduler.pool_class` must be a
:class:`~flux.resource.ResourcePool.ResourcePool` subclass.  The scheduler's
:meth:`~Scheduler._make_pool` helper checks :attr:`~Scheduler.pool_class`
first (highest priority), then ``scheduling.writer`` auto-discovery, and
finally falls back to the default :class:`~flux.resource.ResourcePool.ResourcePool`.

The pool subclass is responsible for its own version dispatch, which keeps the
logic together with the implementation.  The pattern is to map version
integers to version-specific implementation classes in an ``_impl_map`` and
construct the right one in ``__init__`` before calling
``super().__init__(impl_instance)``:

.. code-block:: python

   class RackPool(ResourcePool):
       _impl_map = {1: _RackPoolV1}   # extend here to add Rv2 support

       def __init__(self, R, log=None):
           version = R.get("version", 1) if isinstance(R, Mapping) else 1
           impl_class = self._impl_map.get(version)
           if impl_class is None:
               raise ValueError(f"R version {version} not supported by RackPool")
           super().__init__(impl_class(R, log=log))

The complete worked example of all three hooks — selection, feasibility, and
pool class — is
`t/scheduler/RackPool.py <https://github.com/flux-framework/flux-core/blob/master/t/scheduler/RackPool.py>`__
in the source tree.
`t/scheduler/sched-rack-subclass.py <https://github.com/flux-framework/flux-core/blob/master/t/scheduler/sched-rack-subclass.py>`__
shows the class-attribute binding pattern.


The R.scheduling key
--------------------

R may carry a ``scheduling`` key in its top-level JSON object containing
scheduler-specific topology metadata.  Schedulers use this key to encode
information that is not captured in the standard R execution section — for
example, rack or chassis membership, network topology, or fabric locality.

:class:`~flux.resource.Rv1Pool` propagates the ``scheduling`` key to every
allocated R by default, making it available to downstream consumers.
Schedulers that store rank-indexed data in the key should override
:meth:`~flux.resource.Rv1Pool.Rv1Pool.alloc` to filter it down to the
allocated ranks before returning.

Writer identification
~~~~~~~~~~~~~~~~~~~~~

The ``scheduling`` key is defined by :doc:`rfc:spec_20`.  By convention it
includes a ``writer`` URI identifying the scheduler that created it; a missing
``writer`` implies ``fluxion``.

When a scheduler starts and finds a ``scheduling.writer`` URI in the system R,
:meth:`~Scheduler._pool_class_from_writer` resolves the URI to a pool class
and :meth:`~Scheduler._make_pool` instantiates it automatically.  This enables
a sub-instance scheduler to load the same pool class as the parent without any
explicit configuration — the parent bakes the URI into every allocated R, and
the sub-instance picks it up on startup.

For the auto-discovery to work end-to-end, the pool's
:meth:`~flux.resource.Rv1Pool.Rv1Pool.alloc` override must preserve the
``writer`` key in the trimmed ``scheduling`` it attaches to allocated R.
Keeping the full ``scheduling`` dict (minus trimmed topology entries) is
sufficient — the ``writer`` key is carried along automatically.


Sub-instance resource pools
~~~~~~~~~~~~~~~~~~~~~~~~~~~

In Flux, the R allocated to a job becomes the resource inventory of any
sub-instance that starts inside that job.  A scheduler loaded in the
sub-instance therefore initialises its pool from the allocated R.

The sub-instance's resource module re-ranks the allocated nodes from zero:
if the parent allocated original ranks ``{2, 3}``, the sub-instance sees
them as ranks ``{0, 1}``.  The ``execution.R_lite`` section is updated
automatically, but the ``scheduling`` key is not — its rank references still
use the parent's numbering.  Two invariants must hold for the pool to be
consistent:

1. **Only allocated ranks appear in** ``scheduling``.  Override
   :meth:`~flux.resource.Rv1Pool.Rv1Pool.alloc` to trim the key before the
   result is returned, so each nesting level sees only its own topology:

   .. code-block:: python

      def alloc(self, jobid, request):
          result = super().alloc(jobid, request)
          if result.scheduling:
              result.scheduling = dict(result.scheduling)
              result.scheduling["racks"] = _filter_racks(
                  result.scheduling.get("racks", []), set(result._ranks)
              )
          return result

2. **Ranks in** ``scheduling`` **are re-mapped to match the pool.**
   The sub-instance resource module always re-ranks allocated nodes from zero,
   so pool ranks are always 0..N-1.  After trimming, the n-th rank in
   ``scheduling`` (sorted ascending) corresponds to pool rank ``n``.  Apply
   the sorted-zip unconditionally; when ranks already match it is a no-op:

   .. code-block:: python

      sorted_orig = sorted(rack_map)               # e.g. [2, 3]
      rank_remap = dict(zip(sorted_orig, range(len(self._ranks))))
      rack_map = {rank_remap[o]: rid for o, rid in rack_map.items()}
      scheduling["racks"] = _rerank_racks(scheduling.get("racks", []), rank_remap)

.. note::

   The sorted-zip re-ranking assumes that the n-th node in the parent's
   allocated R (ascending rank order) becomes broker rank ``n`` in the
   sub-instance.  This holds for the typical case of one broker per physical
   node with the default block taskmap.  It can break when a non-default
   taskmap reorders task assignment across nodes (the broker's PMI rank —
   which becomes its broker rank — equals its task rank).  Using hostnames
   to correlate ``scheduling`` entries with pool entries is more robust in
   principle, but does not work when multiple brokers share a node (as in
   test instances), where each host appears more than once.

`t/scheduler/RackPool.py <https://github.com/flux-framework/flux-core/blob/master/t/scheduler/RackPool.py>`__ demonstrates both steps.


API reference
-------------

.. autoclass:: flux.scheduler.Scheduler
   :members:
   :undoc-members:
   :noindex:


.. autoclass:: flux.scheduler.AllocRequest
   :members:
   :noindex:

.. autoclass:: flux.scheduler.PendingJob
   :members:
   :noindex:

.. autoclass:: flux.resource.ResourcePool.ResourcePool
   :members:
   :noindex:
