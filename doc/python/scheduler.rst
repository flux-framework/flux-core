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
job annotations and planning schedulers.


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
   import errno
   from flux.scheduler import Scheduler

   class SimpleScheduler(Scheduler):

       def schedule(self):
           while self._queue:
               job = self._queue[0]
               try:
                   alloc = self.resources.alloc(job.jobid, job.resource_request)
               except OSError as exc:
                   if exc.errno == errno.ENOSPC:
                       break   # not enough resources — wait for free
                   job.request.deny(exc.strerror)
               else:
                   job.request.success(alloc.to_dict())
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
:attr:`~Scheduler.resource_class` (default
:class:`~flux.resource.Rv1Pool`) that represents the entire managed resource
set.  It tracks which resources are up, down, and currently allocated.

The resource request
~~~~~~~~~~~~~~~~~~~~

Each :class:`PendingJob` carries a pool-specific resource request object,
parsed once from the jobspec when the alloc request arrives by
:meth:`~flux.resource.ResourcePool.parse_resource_request`.  The type and
fields of this object are defined by the pool; a custom pool may return any
object its :meth:`~flux.resource.ResourcePool.alloc` method understands.

For the built-in pools (:class:`~flux.resource.Rv1Pool` and
:class:`~flux.resource.Rv1RlistPool`) the request is a ``ResourceRequest``
object with the following fields:

.. code-block:: python

   rr = job.resource_request   # valid for Rv1Pool and Rv1RlistPool only
   rr.nnodes        # minimum node count (0 = any layout)
   rr.nslots        # total slot count
   rr.slot_size     # cores per slot
   rr.gpu_per_slot  # GPUs per slot (0 = none)
   rr.duration      # walltime in seconds (0.0 = unlimited)
   rr.constraint    # RFC 31 constraint dict, or None
   rr.exclusive     # whole-node exclusive allocation
   rr.ncores        # shorthand: nslots * slot_size
   rr.ngpus         # shorthand: nslots * gpu_per_slot

If you need to parse a jobspec outside of :meth:`~Scheduler.alloc`
(e.g., in :meth:`~Scheduler.feasibility_check`), delegate to
the pool:

.. code-block:: python

   rr = self.resources.parse_resource_request(jobspec)

Allocating resources
~~~~~~~~~~~~~~~~~~~~

Pass the jobid and resource request to :meth:`~flux.resource.ResourcePool.alloc`:

.. code-block:: python

   try:
       alloc = self.resources.alloc(job.jobid, job.resource_request,
                                    mode=self._alloc_mode)
   except OSError as exc:
       if exc.errno == errno.ENOSPC:
           # Not enough resources now — queue the request and retry later
           ...
       else:
           # Request can never be satisfied (e.g., EOVERFLOW for node count)
           request.deny(exc.strerror)
           return

The ``mode`` argument selects an allocation strategy; supported values depend
on the resource pool class.  :class:`~flux.resource.Rv1Pool` (the
default) only supports ``"worst-fit"`` (spreads load across nodes).
:class:`~flux.resource.Rv1RlistPool` additionally supports
``"best-fit"`` (packs load) and ``"first-fit"`` (fills by rank order).  The
base class parses the ``alloc-mode=`` module argument and stores it as
:attr:`_alloc_mode`.

RFC 31 constraint expressions (``constraint`` field on the resource request)
are carried through automatically; the resource pool evaluates them when
selecting candidate nodes.

On success, :meth:`~flux.resource.ResourcePool.alloc` returns a resource
pool object containing only the allocated resources.  Call
:meth:`~flux.resource.ResourcePool.to_dict` to convert it to the dict
that :meth:`~AllocRequest.success` expects:

.. code-block:: python

   request.success(alloc.to_dict())

The pool also records the job's end time (derived from ``request.duration``)
internally; :meth:`~flux.resource.ResourcePool.free` and
:meth:`~flux.resource.ResourcePool.estimate_start_time` use this state.

Releasing resources
~~~~~~~~~~~~~~~~~~~

The base class :meth:`~Scheduler.free` calls
:meth:`~flux.resource.ResourcePool.free` automatically — no override is
needed unless the scheduler has additional per-job cleanup to do.

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
  arrives.  For the built-in pools this carries fields for node count, slot count,
  cores and GPUs per slot, duration, constraints, and exclusivity.
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
    Allocation succeeded.  *R* must be a dict (use
    :meth:`~flux.resource.ResourcePool.to_dict` on the alloc result).
    The base class commits R to the KVS asynchronously before sending the
    response to job-manager, ensuring R is safely stored before the job
    can run.

:meth:`request.deny(note=None) <AllocRequest.deny>`
    Permanent denial — the job will never run.  *note* is a human-readable
    reason that appears in :man1:`flux-job` eventlog.  Use this for
    structurally unsatisfiable requests (too many cores, unsupported resource
    type, etc.).

:meth:`request.cancel() <AllocRequest.cancel>`
    The alloc is being cancelled (e.g., the job was cancelled while pending).
    Call this from :meth:`~Scheduler.cancel`.

:meth:`request.annotate(annotations) <AllocRequest.annotate>`
    Send an intermediate annotation update while the job is pending.  May be
    called any number of times before finalizing.  Annotations are visible
    in :man1:`flux-jobs` output.

    RFC 27 defines two standard ``sched`` annotation keys:

    - ``resource_summary`` — free-form string describing the allocated
      resources (e.g., ``"rank[0-1]/core[0-3]"``).  Set this on a
      successful allocation.
    - ``backfill`` — f58-encoded jobid of the head-of-queue job whose
      reservation this job is filling.  Set only on backfilled jobs.
    - ``t_estimate`` — estimated start time as a Unix epoch float, or
      ``null`` to clear.  Set this on pending jobs that have a shadow
      reservation; clear it on allocation.

    Example (planning scheduler posting a reservation estimate):

    .. code-block:: python

       # RFC 27 sched annotation: t_estimate is wall clock seconds since epoch
       request.annotate({"sched": {"t_estimate": shadow_time}})

    Clear annotations when a pending job is cancelled:

    .. code-block:: python

       request.annotate({"sched": {"resource_summary": None,
                                   "t_estimate": None}})

    Note: scheduler annotations are **not** recorded in the job eventlog, so
    :man1:`flux-job` ``wait-event annotations`` cannot be used to wait for
    them in tests; instead poll :man1:`flux-jobs` ``-no {annotations.sched.field}``.


Override points
---------------

Subclass :class:`Scheduler` and override any of the
following methods:

:meth:`hello(self, jobid, priority, userid, t_submit, R) <Scheduler.hello>`
    Called once per running job during startup, before the reactor starts.
    The default marks *R* as allocated in the resource pool.  Override to
    also record per-job state (e.g., job duration for a planning scheduler).

:meth:`alloc(self, request, jobid, priority, userid, t_submit, jobspec) <Scheduler.alloc>`
    Called when job-manager requests an allocation.  The default calls
    :meth:`~flux.resource.ResourcePool.parse_resource_request` on the pool
    to parse *jobspec* and pushes a :class:`PendingJob` onto
    :attr:`_queue`.  Override to reject jobs before they enter the queue.

:meth:`free(self, jobid, R, final=False) <Scheduler.free>`
    Called when resources are released for a completed or cancelled job.
    *final* is ``True`` on the last free for the job (when epilog has
    completed).  The default calls :meth:`~flux.resource.ResourcePool.free`
    on the pool.  Override (calling :meth:`~Scheduler.free`
    via ``super()``) to also remove per-job state or log the event.

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

    Example::

        def schedule(self):
            while self._queue:
                job = self._queue[0]
                try:
                    alloc = self.resources.alloc(job.jobid, job.resource_request)
                except OSError as exc:
                    if exc.errno == errno.ENOSPC:
                        break   # head blocked; stop (FIFO)
                    job.request.deny(exc.strerror)
                else:
                    job.request.success(alloc.to_dict())
                heapq.heappop(self._queue)

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
    throughput — :meth:`~Scheduler.forecast` is rate-limited to at most
    once per :attr:`~Scheduler.FORECAST_PERIOD` seconds so that annotation
    work is kept off the critical scheduling path during bursts.

    Example (annotating the head-of-queue job with its estimated start time):

    .. code-block:: python

       def forecast(self):
           if not self._queue:
               return
           head = sorted(self._queue)[0]
           t = self.resources.estimate_start_time(head.resource_request)
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

:meth:`~Scheduler.forecast` is triggered after every
:meth:`~Scheduler.schedule` call, but rate-limited by a separate one-shot
timer so that annotation work does not accumulate on the critical path during
scheduling bursts.

The mechanism is simpler than the adaptive scheduling timer: after each
:meth:`~Scheduler.schedule` call the base class calls
:meth:`~Scheduler._request_forecast`.  If the forecast timer is not already
armed it is set to fire after :attr:`~Scheduler.FORECAST_PERIOD` seconds
(default ``1.0``).  Subsequent :meth:`~Scheduler._request_forecast` calls
while the timer is armed are no-ops, so a burst of N scheduling events
results in exactly one :meth:`~Scheduler.forecast` call roughly one second
after the burst begins.

The period can be tuned per-instance at load time::

    flux module load sched-fifo forecast-period=0.5

or overridden on the subclass:

.. code-block:: python

   class MyScheduler(Scheduler):
       FORECAST_PERIOD = 2.0   # run forecast() at most once every 2 seconds

Because :meth:`~Scheduler.forecast` runs asynchronously after
:meth:`~Scheduler.schedule`, the data it reads from ``self._queue`` and
``self.resources`` reflects the state at the time the timer fires, not the
time the triggering scheduling event occurred.  This is correct: annotations
should reflect the *current* queue state, not a stale snapshot from a burst
that has since been partially resolved.


Module arguments
----------------

Arguments passed after the module path at load time arrive as ``*args``
in :func:`mod_main` and are forwarded to ``__init__``:

.. code-block:: console

   $ flux module load my-sched.py queue-depth=8 alloc-mode=best-fit resource-class=Rv1RlistPool

The base class automatically handles ``queue-depth=``, ``resource-class=``,
``alloc-mode=``, and ``forecast-period=`` arguments.  ``alloc-mode=`` is
stored as :attr:`~Scheduler._alloc_mode` for use in
:meth:`~flux.resource.Rv1Pool.alloc` calls; ``forecast-period=`` sets
:attr:`~Scheduler.FORECAST_PERIOD` on the instance.  Subclasses only need
to parse their own custom arguments:

.. code-block:: python

   def __init__(self, h, *args):
       super().__init__(h, *args)   # handles queue-depth=, resource-class=, alloc-mode=
       for arg in args:
           if arg.startswith("my-option="):
               self._my_option = arg[10:]


Writing a planning scheduler
----------------------------

A planning scheduler tracks resource reservations for running jobs and can
make forward-time allocation decisions — for instance, promising a job will
start at a specific time even if resources are not available right now.
The :class:`Scheduler` base class provides the hooks that planning schedulers
need.  In particular, annotation work (posting ``sched.t_estimate`` on pending
jobs) belongs in :meth:`~Scheduler.forecast` rather than
:meth:`~Scheduler.schedule` so that it is rate-limited and kept off the
critical scheduling path; see `Forecast deferral`_ for details.

The pending queue
~~~~~~~~~~~~~~~~~

:attr:`~Scheduler._queue` is a :mod:`heapq` min-heap.  The
heap invariant guarantees that ``self._queue[0]`` is always the
highest-priority pending job, and :func:`heapq.heappop` removes jobs in
priority order — but **the underlying list is not sorted beyond the root**.
Iterating ``self._queue`` directly does not visit jobs in priority order.

To traverse pending jobs in priority order (as a planning scheduler must when
computing backfill candidates or updating annotations), use
:func:`sorted`:

.. code-block:: python

   for job in sorted(self._queue):   # highest priority first
       ...

:func:`sorted` respects :meth:`~PendingJob.__lt__` and costs
O(n log n); for most scheduler queue sizes this is negligible.

Job expirations
~~~~~~~~~~~~~~~

The base class dispatches ``sched.expiration`` requests from job-manager
to :meth:`~Scheduler.expiration`.  The default accepts all
updates unconditionally.  A planning scheduler should override this to keep
its internal state consistent before responding (see `Estimating start time`_
below for the recommended pattern).

Hello with existing jobs
~~~~~~~~~~~~~~~~~~~~~~~~

The :meth:`~Scheduler.hello` override receives each running
job's current resource pool object.  A planning scheduler should
record the job's allocation and expiration so it can account for those
resources when estimating start times:

.. code-block:: python

   def hello(self, jobid, priority, userid, t_submit, R):
       super().hello(jobid, priority, userid, t_submit, R)
       self.handle.log(syslog.LOG_DEBUG, f"hello: {jobid} alloc={R.dumps()}")

The pool allocation object *R* is opaque to the scheduler — its exact type
matches :attr:`~Scheduler.resource_class`.  The base class
calls :meth:`~flux.resource.ResourcePool.register_alloc` to record the
allocation and expiration in the pool's internal job state, so no separate
per-job tracking is needed in the scheduler subclass.

Estimating start time
~~~~~~~~~~~~~~~~~~~~~

Planning schedulers call
:meth:`~flux.resource.ResourcePool.estimate_start_time` on the pool to
determine when enough resources will be free for a pending job.  The pool
consults its own internal job state — populated by
:meth:`~flux.resource.ResourcePool.register_alloc` and
:meth:`~flux.resource.ResourcePool.alloc` — so no external running dict is
needed:

.. code-block:: python

   shadow = self.resources.estimate_start_time(head.resource_request)
   # shadow is a wall-clock epoch float, or None if start time is unknown

The pool owns this estimate because it understands its own resource model and
tracks end times for all jobs internally.  For the built-in Rv1-era pools this
is the EASY backfill *shadow-time* calculation (Mu'alem & Feitelson, 2001):
sort tracked jobs by end time, accumulate freed resources until the request is
satisfied.  A topology-aware pool could instead reason about which specific
topology regions are freed as each job completes.

When job-manager updates a job's expiration, delegate to the pool:

.. code-block:: python

   def expiration(self, msg, jobid, expiration):
       self.resources.update_expiration(jobid, expiration)
       self.handle.respond(msg, None)

Resource pool class
~~~~~~~~~~~~~~~~~~~

For sophisticated planning algorithms, the resource pool implementation can
be replaced by setting :attr:`~Scheduler.resource_class` to
any class that inherits from :class:`~flux.resource.ResourcePool`.
This allows using a custom topology-aware pool implementation without
changing any of the base class machinery.


Extending the resource model
----------------------------

The built-in pools (:class:`~flux.resource.Rv1Pool` and
:class:`~flux.resource.Rv1RlistPool`) represent resources as a flat collection
of nodes, each with a core count and optional GPU count.  This is compact and
sufficient for a wide range of workloads, but loses the topology hierarchy of
the underlying hardware.  On machines with deep, non-uniform topology —
multiple compute dies per node, each with its own GPU memory, configurable in
different partition modes — a flat representation cannot express placement
constraints between hardware components within a node, nor can a graph-based
scheduler like Fluxion use it directly as a bootstrap.

A richer resource representation would capture the full hardware hierarchy:
nodes contain compute dies, dies contain core-complexes, core-complexes
contain cores, and each die may carry one or more accelerators with their own
memory capacity.  Jobs could then request topology-aware placement (e.g.,
cores within the same die, or a set of cores co-located with a specific
accelerator), and the pool would match those requests against the hierarchy
rather than a flat node count.

The :class:`~flux.resource.ResourcePool` protocol is the clean insertion
point for such an extension.  Because the pool owns parsing, allocation, and
serialization — and because scheduler policy code accesses resources only
through the pool interface — a richer pool implementation can be substituted
without modifying the base class or any scheduling policy code.

The interface
~~~~~~~~~~~~~

A custom resource class must inherit from :class:`~flux.resource.ResourcePool`
and implement its abstract methods:

.. code-block:: python

   from flux.resource import ResourcePool

   class MyPool(ResourcePool):
       # Set True if GPU scheduling is supported, False otherwise.
       supports_gpu = True

       # Set of mode strings accepted by alloc(); None skips startup validation.
       supported_alloc_modes = frozenset({"worst-fit"})

       def __init__(self, R):
           """Construct from a parsed R dict or JSON string."""
           ...

       # --- State management (called by base class) ---

       def mark_up(self, ids):    ...  # ids is an idset string or "all"
       def mark_down(self, ids):  ...

       @property
       def expiration(self):      ...  # float seconds since epoch, 0 = none
       @expiration.setter
       def expiration(self, v):   ...

       def remove_ranks(self, ranks): ...  # called on resource shrink

       def register_alloc(self, jobid, R): ...  # reconnect: register existing alloc
       def copy(self):            ...  # fresh copy, allocation state cleared
       def copy_allocated(self):  ...  # copy containing only allocated resources
       def copy_down(self):       ...  # copy containing only down resources
       def to_dict(self):         ...  # return parsed R dict
       def dumps(self):           ...  # compact human-readable summary
       def set_starttime(self, t): ...
       def set_expiration(self, t): ...

       # --- Called by scheduler subclasses ---

       def parse_resource_request(self, jobspec): ...
       def alloc(self, jobid, request, mode=None): ...
       def free(self, jobid): ...
       def check_feasibility(self, request): ...
       def update_expiration(self, jobid, expiration): ...
       def estimate_start_time(self, request): ...

The pool owns the full resource lifecycle: it parses the jobspec via
:meth:`parse_resource_request`, allocates via :meth:`alloc`, returns
resources via :meth:`free`, checks structural feasibility via
:meth:`check_feasibility`, and estimates future start times via
:meth:`estimate_start_time`.  All job tracking is internal to the pool —
scheduler policy code never maintains a separate running-job dict.  As the
resource representation and jobspec format evolve, only the pool
implementation needs updating; the base class and all existing scheduler
policy code remain unchanged.

Wiring it in
~~~~~~~~~~~~

Set :attr:`~Scheduler.resource_class` on the scheduler
subclass:

.. code-block:: python

   from my_pool import MyPool

   class MyScheduler(Scheduler):
       resource_class = MyPool

       def schedule(self):
           ...

The ``resource-class=`` module argument only recognises the two built-in
names (:class:`~flux.resource.Rv1Pool` and
:class:`~flux.resource.Rv1RlistPool`).  A custom class must be wired in at
the subclass level as shown above; it cannot be selected at load time without
additional argument parsing in ``__init__``.

Unit-testing the resource class
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Because the resource class is a plain Python object, it can be tested
independently of a running Flux instance.  Construct it directly from a
synthetic R dict and exercise :meth:`~flux.resource.Rv1Pool.alloc`,
:meth:`~flux.resource.Rv1Pool.free`, and the up/down state methods:

.. code-block:: python

   import unittest
   from my_pool import MyPool, MyResourceRequest

   R = { ... }  # synthetic R in your pool's format

   class TestMyPool(unittest.TestCase):
       def setUp(self):
           self.pool = MyPool(R)

       def test_alloc_one_slot(self):
           rr = MyResourceRequest(...)
           alloc = self.pool.alloc(1, rr)
           self.assertEqual(alloc.count("core"), 1)

See :file:`t/python/t0039-rv1pool.py` in the source tree for a comprehensive
example of this pattern applied to :class:`~flux.resource.Rv1Pool`.


Logging
-------

Use the broker log via the handle:

.. code-block:: python

   import syslog
   self.handle.log(syslog.LOG_DEBUG, f"alloc: {jobid}: {alloc.dumps()}")
   self.handle.log(syslog.LOG_ERR,   f"unexpected error: {exc}")

Log messages appear in :man1:`flux-dmesg` and the broker's stderr.


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
example covering alloc modes, annotations, priority, cancel, drain/undrain,
and the hello/reload protocol.


API reference
-------------

.. autoclass:: flux.scheduler.Scheduler
   :members:
   :undoc-members:


.. autoclass:: flux.scheduler.AllocRequest
   :members:

.. autoclass:: flux.scheduler.PendingJob
   :members:

.. autoclass:: flux.resource.ResourcePool.ResourcePool
   :members:
