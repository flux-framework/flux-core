###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Base class for Python Flux scheduler broker modules.

Provides the RFC 27 scheduler protocol scaffolding so that subclasses
only need to implement scheduling policy:

- Registers the ``sched`` and ``feasibility`` dynamic services
- Manages the ``resource.acquire`` streaming RPC for resource state
- Performs the hello/ready handshake with job-manager on startup
- Dispatches alloc, free, cancel, prioritize, expiration, and
  resource-status messages to overridable methods

The base class maintains a :class:`PendingJob` heap queue (:attr:`_queue`)
and provides default implementations of :meth:`alloc`, :meth:`free`,
:meth:`cancel`, and :meth:`prioritize`.  A minimal scheduler only needs
to override :meth:`schedule`::

    import heapq
    from flux.resource import InsufficientResources, InfeasibleRequest
    from flux.scheduler import Scheduler

    class MyScheduler(Scheduler):

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
                yield   # hand control to the reactor

    def mod_main(h, *args):
        MyScheduler(h, *args).run()
"""

import errno
import functools
import heapq
import inspect
import time

from _flux._core import ffi, lib
from flux.brokermod import BrokerLogger, BrokerModule, request_handler
from flux.constants import FLUX_RPC_STREAMING
from flux.job import JobID, job_raise_async
from flux.kvs import KVSTxn
from flux.kvs import commit_async as kvs_commit_async
from flux.kvs import get_key_direct as kvs_get
from flux.resource import InfeasibleRequest
from flux.resource.ResourcePool import ResourcePool


@functools.total_ordering
class PendingJob:
    """Pending allocation request held in the scheduler queue.

    Created by the default :meth:`Scheduler.alloc` implementation and stored
    in :attr:`Scheduler._queue`.  Subclasses may access the queue directly
    (it is a ``heapq`` min-heap ordered by :meth:`__lt__`).

    Attributes:
        jobid (int): The job ID.
        priority (int): Job priority (higher is more urgent).
        t_submit (float): Submission time (seconds since epoch).
        request (:class:`AllocRequest`): The open RFC 27 allocation request.
        resource_request: Pool-specific parsed resource request returned by
            :meth:`~ResourcePool.parse_resource_request`; passed to
            :meth:`~ResourcePool.alloc` when scheduling.
    """

    __slots__ = (
        "jobid",
        "priority",
        "t_submit",
        "request",
        "resource_request",
        "_last_annotation",
    )

    def __init__(self, jobid, priority, t_submit, request, resource_request):
        self.jobid = jobid
        self.priority = priority
        self.t_submit = t_submit
        self.request = request
        self.resource_request = resource_request
        self._last_annotation = None

    def __eq__(self, other):
        if not isinstance(other, PendingJob):
            return NotImplemented
        return self.priority == other.priority and self.jobid == other.jobid

    def __lt__(self, other):
        # Higher priority first; tie-break by lower jobid (FIFO)
        if self.priority != other.priority:
            return self.priority > other.priority
        return self.jobid < other.jobid


# ResourcePool is defined in flux.resource.pool to avoid circular imports.
# Re-exported here so existing code can import it from either location.
__all__ = ["ResourcePool", "PendingJob", "AllocRequest", "Scheduler"]


# RFC 27 alloc response type codes
_ALLOC_SUCCESS = 0
_ALLOC_ANNOTATE = 1
_ALLOC_DENY = 2
_ALLOC_CANCEL = 3


class AllocRequest:
    """Represents a pending allocation request from the job-manager.

    Passed to :meth:`Scheduler.alloc` and typically stored in the
    scheduler's pending queue until resources become available.  Call
    :meth:`success`, :meth:`deny`, or :meth:`cancel` to finalize the
    request; call :meth:`annotate` to update job annotations while it
    is pending.

    Attributes:
        jobid (int): The job ID for this allocation request.
    """

    __slots__ = ("_scheduler", "_msg", "jobid", "_annotated_sched_keys", "_finalized")

    def __init__(self, scheduler, msg):
        self._scheduler = scheduler
        self._msg = msg
        self.jobid = msg.payload["id"]
        self._annotated_sched_keys = set()
        self._finalized = False

    def success(self, R, annotations=None, clear=True):
        """Finalize the request with a successful allocation.

        Args:
            R: The allocated resources, either as a resource pool object
                (anything with a ``to_dict()`` method) or a pre-converted
                dict.  Pool objects are converted automatically.
            annotations (dict, optional): Scheduler annotations to attach.
            clear (bool): If True (the default), any ``sched`` annotation keys
                set via :meth:`annotate` while the job was pending are
                automatically nulled in the success response, unless overridden
                by *annotations*.  This ensures pending-state annotations
                (e.g. ``reason_pending``, ``t_estimate``) are cleaned up
                without each scheduler needing to track and clear them manually.
        """
        self._finalized = True
        if not isinstance(R, dict):
            R = R.to_dict()
        if clear and self._annotated_sched_keys:
            # Start with nulls for all previously-annotated keys, then overlay
            # the caller's annotations so explicitly-set keys take precedence.
            sched = {k: None for k in self._annotated_sched_keys}
            sched.update((annotations or {}).get("sched", {}))
            annotations = {**(annotations or {}), "sched": sched}
        self._scheduler.alloc_success(self._msg, R, annotations)

    def deny(self, note=None):
        """Finalize the request with a permanent denial.

        Args:
            note (str, optional): Human-readable reason for the denial.
        """
        self._finalized = True
        self._scheduler.alloc_deny(self._msg, note)

    def cancel(self):
        """Finalize the request indicating it was cancelled."""
        self._finalized = True
        self._scheduler.alloc_cancel(self._msg)

    def annotate(self, annotations):
        """Send an annotation update for this pending request.

        May be called any number of times before the request is finalized.
        Keys set here are tracked so that :meth:`success` can automatically
        clear them.  Calls after the request is finalized are silently
        ignored to prevent stale annotations from a still-running forecast
        generator reaching the job-manager after a cancel.

        Args:
            annotations (dict): Annotation dict to attach to the job.
        """
        if self._finalized:
            return
        self._annotated_sched_keys.update((annotations or {}).get("sched", {}).keys())
        self._scheduler.alloc_annotate(self._msg, annotations)


class Scheduler(BrokerModule):
    """Base class for Python scheduler broker modules.

    Handles the RFC 27 protocol scaffolding so that subclasses only need
    to implement scheduling policy.

    The base class maintains a pending-job queue (:attr:`_queue`, a
    ``heapq`` of :class:`PendingJob` ordered by priority then jobid) and
    provides default implementations of :meth:`alloc` (parse jobspec and
    enqueue), :meth:`free` (release resources), :meth:`cancel` (dequeue
    and cancel), and :meth:`prioritize` (re-sort the queue).  Subclasses
    must override :meth:`schedule` to implement their allocation policy,
    and may override any of the defaults.

    Subclasses may also override:

    - :meth:`hello` — called once per running job during startup (optional;
      default marks resources allocated)
    - :meth:`resource_update` — called after resource state changes
    - :meth:`feasibility_check` — called for ``feasibility.check`` RPCs
    - :meth:`forecast` — called after :meth:`schedule` to annotate pending
      jobs with forward-looking estimates such as ``sched.t_estimate``

    Subclasses may set the class attributes:

    - ``queue_depth`` — maximum pending alloc requests: a positive integer
      (e.g. ``8``) for limited mode, or the string ``"unlimited"`` (default).
      The base class translates this to the wire format automatically.
      End users may override at load time with ``queue-depth=N|unlimited``.

    Alloc requests are represented by :class:`AllocRequest` objects passed
    to :meth:`alloc`.  Call ``request.success(R)``, ``request.deny(note)``,
    ``request.cancel()``, or ``request.annotate(d)`` to respond.

    The current resource pool is available as :attr:`resources`.  Resource
    state is updated automatically from ``resource.acquire`` and
    :meth:`resource_update` is called after each update.
    """

    #: Maximum number of pending alloc requests the job-manager will queue
    #: for this scheduler.  Set to a positive integer (e.g. ``8``) for
    #: ``limited`` mode or to the string ``"unlimited"`` for unlimited mode.
    #: The :class:`Scheduler` base class translates this to the wire format
    #: sent in ``job-manager.sched-ready``; subclasses and end users should
    #: never need to know about the internal ``"limited=N"`` representation.
    #: End users may override at load time with ``queue-depth=N|unlimited``.
    queue_depth = "unlimited"

    #: Maximum scheduling delay used when coalescing bursts of alloc requests.
    #: The adaptive timer will not exceed this value regardless of how long
    #: ``schedule()`` takes.  1 second is a reasonable upper bound for most
    #: HPC schedulers; lower it if worst-case latency matters more than
    #: throughput during large submission bursts.
    SCHED_DELAY_MAX = 1.0

    #: Exponential weighted moving average (EWMA) smoothing factor for the
    #: adaptive scheduling timer.  Higher values react faster to changes in
    #: burst rate and scheduling cost; lower values are more stable.
    #: 0.25 converges in roughly 4 samples.
    SCHED_EWMA_ALPHA = 0.25

    #: If True (the default), send ``partial-ok: True`` in the hello RPC so
    #: that job-manager may report partially-freed ranks for running jobs.
    #: Set to False (or pass ``test-hello-nopartial`` at load time on
    #: subclasses that support it) to disable partial-ok behaviour.
    hello_partial_ok = True

    #: Extra keyword arguments forwarded to :class:`~flux.resource.ResourcePool`
    #: (and on to the pool implementation) at construction time.  Subclasses
    #: that accept pool-specific load-time options should parse them in
    #: ``__init__`` and store the result here, e.g.
    #: ``self.pool_kwargs = {"opt": value}``.
    pool_kwargs: dict = {}

    def __init_subclass__(cls, **kwargs):
        # Automatically call _reject_unknown_args() after each subclass
        # __init__ returns, so that unrecognised module arguments are caught
        # and reported at load time (before set_running(), where errors
        # propagate back to the flux module load command line).
        #
        # Only classes that explicitly define __init__ are wrapped; classes
        # that inherit __init__ from a parent already have the check baked in
        # via the parent's wrapper.
        #
        # The type(self) is _cls guard ensures the check only fires in the
        # wrapper belonging to the actual instantiated class.  This means
        # that when a subclass calls super().__init__(), the parent's wrapper
        # skips the check (leaving self._pending_args intact for the subclass
        # to process), and only the leaf wrapper triggers the final check.
        # Multi-level scheduler inheritance (e.g. a test scheduler subclassing
        # FIFOScheduler) therefore works correctly at any depth.
        super().__init_subclass__(**kwargs)
        if "__init__" in cls.__dict__:
            original = cls.__init__

            def wrapped(self, h, *args, _orig=original, _cls=cls):
                _orig(self, h, *args)
                if type(self) is _cls:
                    self._reject_unknown_args()

            cls.__init__ = wrapped

    def __init__(self, h, *args):
        super().__init__(h, *args)
        self.log.level = "info"
        self._resources = None
        self._acquire_rpc = None
        self._queue = []  # heapq of PendingJob, ordered by PendingJob.__lt__
        # Adaptive scheduling timer: defers schedule() calls to coalesce bursts.
        # _sched_pending: True while the timer is armed (prevents re-arming).
        # _sched_delay: current one-shot delay in seconds (0 = next iteration).
        # _sched_duration_ewma: exponential moving average of schedule() execution time.
        # _sched_interval_ewma: exponential moving average of time between _request_schedule() calls.
        # _sched_last_request: monotonic timestamp of last _request_schedule().
        self._sched_pending = False
        self._sched_delay = 0.0
        self._sched_duration_ewma = 0.0
        self._sched_interval_ewma = 0.0
        self._sched_last_request = None
        self._sched_timer = h.timer_watcher_create(0.0, self._on_sched_timer)
        # Generator-based scheduling: when schedule() returns a generator,
        # the base class steps through it one yield at a time so other reactor
        # events are handled between yields.
        # _sched_generator: active generator, or None when idle.
        # _sched_idle: keeps the reactor spinning while a pass is active.
        # _sched_check: fires each reactor iteration to advance the generator.
        self._sched_generator = None
        self._sched_idle = h.idle_watcher_create()
        self._sched_check = h.check_watcher_create(self._on_sched_check)
        # Generator-based forecast: runs immediately after each schedule() pass.
        # With forecast() as a generator the reactor remains live between
        # annotations, and a new scheduling event aborts any in-progress pass,
        # so no separate rate-limiting timer is needed.
        # _forecast_generator: active generator, or None when idle.
        # _forecast_idle: keeps the reactor spinning while a pass is active.
        # _forecast_check: fires each reactor iteration to advance the generator.
        # _forecast_pending: True if a schedule pass completed while a forecast
        #   was running; causes _on_forecast_check to restart forecast on finish.
        self._forecast_generator = None
        self._forecast_pending = False
        self._forecast_idle = h.idle_watcher_create()
        self._forecast_check = h.check_watcher_create(self._on_forecast_check)
        # Scheduling statistics, exposed via the stats-get RPC.
        self._sched_passes = 0
        self._sched_yields = 0
        self._forecast_passes = 0
        self._forecast_yields = 0
        self._pending_args = []
        for arg in args:
            if arg.startswith("queue-depth="):
                val = arg[12:]
                if val == "unlimited":
                    self.queue_depth = "unlimited"
                else:
                    try:
                        n = int(val)
                        if n < 1:
                            raise ValueError
                        self.queue_depth = n
                    except ValueError:
                        raise ValueError(
                            f"queue-depth={val!r} is invalid: "
                            f"expected a positive integer or 'unlimited'"
                        )
            elif arg == "mode=unlimited":
                self.queue_depth = "unlimited"
            elif arg.startswith("mode=limited="):
                val = arg[13:]
                try:
                    n = int(val)
                    if n < 1:
                        raise ValueError
                    self.queue_depth = n
                except ValueError:
                    raise ValueError(
                        f"mode=limited= requires a positive integer, got {val!r}"
                    )
            elif arg.startswith("log-level="):
                name = arg[10:].lower()
                try:
                    self.log.level = name
                except ValueError:
                    raise ValueError(
                        f"log-level={name!r} is invalid: "
                        f"expected one of {', '.join(BrokerLogger.LEVEL_NAMES)}"
                    )
            else:
                self._pending_args.append(arg)

    def _reject_unknown_args(self):
        """Raise ValueError if any unrecognized module arguments remain.

        Call this at the end of a subclass ``__init__`` after processing
        any subclass-specific arguments from :attr:`_pending_args`.
        """
        if self._pending_args:
            raise ValueError(
                f"unknown argument {self._pending_args[0]!r}: "
                f"built-in options are queue-depth, log-level"
            )

    # ------------------------------------------------------------------
    # Public interface: resource state
    # ------------------------------------------------------------------

    @property
    def resources(self):
        """The current resource pool."""
        return self._resources

    # ------------------------------------------------------------------
    # Adaptive scheduling timer
    # ------------------------------------------------------------------

    def _request_schedule(self):
        """Request a scheduling pass, coalescing bursts via an adaptive timer.

        Records the time of the request and updates the inter-request interval
        exponential moving average, then delegates to :meth:`start_schedule`.

        Subclasses that want to replace the generator-based scheduling driver
        should override :meth:`start_schedule` rather than this method.
        Overriding :meth:`start_schedule` preserves the EWMA interval tracking
        done here, which is needed for accurate burst-coalescing delay
        computation.
        """
        if not self._queue:
            return
        now = time.monotonic()
        if self._sched_last_request is not None:
            interval = now - self._sched_last_request
            a = self.SCHED_EWMA_ALPHA
            self._sched_interval_ewma = (
                a * interval + (1 - a) * self._sched_interval_ewma
            )
        self._sched_last_request = now
        self.start_schedule()

    def start_schedule(self):
        """Arm the scheduling timer, aborting any in-progress generator pass.

        Called by :meth:`_request_schedule` after the EWMA interval update.
        Override this method (not :meth:`_request_schedule`) to replace the
        generator-based scheduling loop with an alternative driver, while still
        benefiting from the burst-coalescing delay computed in
        :meth:`_request_schedule`.

        The default implementation:

        1. Closes any in-progress :meth:`schedule` generator and stops its
           yield watchers (queue or resource state is about to change so
           in-progress allocations are stale).  An in-progress
           :meth:`forecast` generator is left running to completion: forecast
           estimates are approximate by nature and a slightly stale
           ``t_estimate`` is more useful than none at all.
        2. Arms the one-shot scheduling timer with the current adaptive delay
           if it is not already pending.  Subsequent calls while the timer is
           armed are no-ops, coalescing all events in the window into a single
           :meth:`schedule` invocation.
        """
        # Abort the in-progress schedule generator: queue or resource state is
        # about to change so any in-progress allocations are stale.
        # The schedule timer will be re-armed below with the adaptive settling
        # delay so the new pass starts cleanly.
        # The forecast generator (if any) is intentionally left running: its
        # simulation snapshot was taken at pass start and remains internally
        # consistent, and a slightly stale t_estimate is better than none.
        if self._sched_generator is not None:
            self._sched_generator.close()
            self._sched_generator = None
            self._sched_idle.stop()
            self._sched_check.stop()
        if not self._sched_pending:
            self._sched_pending = True
            self._sched_timer.reset(after=self._sched_delay)

    def _on_sched_timer(self, *_):
        """Timer callback: invoke schedule() and dispatch the result.

        If ``schedule()`` returns a generator the base class advances it one
        yield per reactor iteration via the idle/check watcher pair so that
        other events are handled between yields.  If it returns ``None``
        (non-generator subclasses) the call completes synchronously as before.
        """
        self._sched_pending = False
        t0 = time.monotonic()
        result = self.schedule()
        # A scheduler implementation uses one style consistently: either
        # generator-based (schedule() returns a generator) or synchronous
        # (returns None).  The check is per-call for convenience; mixing
        # the two styles within a single scheduler is not supported.
        if inspect.isgenerator(result):
            # Hand off to the yield watchers.
            self._sched_generator = result
            self._sched_idle.start()
            self._sched_check.start()
        else:
            # Non-generator (old-style) schedule(): update EWMA synchronously.
            self._sched_passes += 1
            self._update_sched_ewma(time.monotonic() - t0)
            self._request_forecast()

    def _on_sched_check(self, *_):
        """Check-watcher callback: advance the generator by one yield."""
        try:
            next(self._sched_generator)
            self._sched_yields += 1
        except StopIteration:
            self._sched_passes += 1
            self._sched_generator = None
            self._sched_idle.stop()
            self._sched_check.stop()
            # EWMA is not updated for generator-based schedulers: burst-coalescing
            # delay remains 0 and the timer fires on the next reactor iteration.
            self._request_forecast()

    def _update_sched_ewma(self, duration):
        """Update the adaptive scheduling delay from a completed pass duration."""
        a = self.SCHED_EWMA_ALPHA
        self._sched_duration_ewma = a * duration + (1 - a) * self._sched_duration_ewma

        # Burst detection: requests arrive faster than schedule() runs.
        # Add a coalescing delay proportional to the schedule duration so
        # that the scheduler runs at most once per scheduling cycle.
        # When the burst ends (interval ≥ duration) reset to zero.
        old_delay = self._sched_delay
        if (
            self._sched_interval_ewma > 0
            and self._sched_interval_ewma < self._sched_duration_ewma
        ):
            self._sched_delay = min(self._sched_duration_ewma, self.SCHED_DELAY_MAX)
        else:
            self._sched_delay = 0.0

        if self._sched_delay != old_delay:
            if self._sched_delay > 0:
                self.log.debug(
                    f"sched: burst detected, coalescing delay="
                    f"{self._sched_delay * 1000:.1f}ms",
                )
            else:
                self.log.debug("sched: burst ended, delay=0")

    # ------------------------------------------------------------------
    # Forecast
    # ------------------------------------------------------------------

    def _request_forecast(self):
        """Start a forecast pass immediately after a schedule() pass completes.

        If a forecast generator is already in progress it is left running to
        completion; ``_forecast_pending`` is set so that ``_on_forecast_check``
        will restart the forecast with the updated queue once the current pass
        finishes.
        """
        if not self._queue:
            return
        if self._forecast_generator is not None:
            self._forecast_pending = True  # restart after current pass finishes
            return
        self._forecast_pending = False
        result = self.forecast()
        # Same style consistency applies to forecast(): generator or
        # synchronous, but not mixed within a single scheduler.
        if inspect.isgenerator(result):
            self._forecast_generator = result
            self._forecast_idle.start()
            self._forecast_check.start()

    def _on_forecast_check(self, *_):
        """Check-watcher callback: advance the forecast generator by one yield."""
        try:
            next(self._forecast_generator)
            self._forecast_yields += 1
        except StopIteration:
            self._forecast_passes += 1
            self._forecast_generator = None
            self._forecast_idle.stop()
            self._forecast_check.stop()
            if self._forecast_pending:
                self._request_forecast()

    # ------------------------------------------------------------------
    # Alloc response implementation (called via AllocRequest)
    # ------------------------------------------------------------------

    def alloc_success(self, msg, R, annotations=None):
        """Commit R to KVS and send an alloc success response.

        Typically called via :meth:`AllocRequest.success`.  *R* must be a
        dict (parsed R JSON object).  The alloc response to job-manager is
        sent only after R is safely stored in KVS.

        Args:
            msg: The alloc request :class:`~flux.message.Message`.
            R (dict): The allocated resource set as a parsed JSON object.
            annotations (dict, optional): Scheduler annotations to include.
        """
        jobid = msg.payload["id"]

        # Compute KVS key: job.<f58id>.R
        buf = ffi.new("char[128]")
        if lib.flux_job_kvs_key(buf, 128, jobid, b"R") < 0:
            raise OSError("flux_job_kvs_key failed")
        key = ffi.string(buf).decode()

        txn = KVSTxn(self.handle)
        txn.put(key, R)
        f = kvs_commit_async(self.handle, _kvstxn=txn)
        f.then(self._alloc_commit_continuation, (msg, R, annotations))

    def _alloc_commit_continuation(self, future, args):
        """Send the alloc response after the KVS commit completes."""
        msg, R_dict, annotations = args
        try:
            future.get()
        except OSError as exc:
            self.log.error(f"alloc: KVS commit failed: {exc}")
            self.stop_error()
            return
        resp = {"id": msg.payload["id"], "type": _ALLOC_SUCCESS, "R": R_dict}
        if annotations is not None:
            resp["annotations"] = annotations
        self.handle.respond(msg, resp)

    def alloc_deny(self, msg, note=None):
        """Send an alloc denial response.  Typically called via :meth:`AllocRequest.deny`.

        Args:
            msg: The alloc request :class:`~flux.message.Message`.
            note (str, optional): Human-readable reason for the denial.
        """
        resp = {"id": msg.payload["id"], "type": _ALLOC_DENY}
        if note is not None:
            resp["note"] = note
        self.handle.respond(msg, resp)

    def alloc_cancel(self, msg):
        """Send an alloc cancel response.  Typically called via :meth:`AllocRequest.cancel`.

        Args:
            msg: The original alloc request :class:`~flux.message.Message`.
        """
        self.handle.respond(msg, {"id": msg.payload["id"], "type": _ALLOC_CANCEL})

    def alloc_annotate(self, msg, annotations):
        """Send an alloc annotation update.  Typically called via :meth:`AllocRequest.annotate`.

        Args:
            msg: The alloc request :class:`~flux.message.Message`.
            annotations (dict): Annotation dict to attach to the job.
        """
        self.handle.respond(
            msg,
            {
                "id": msg.payload["id"],
                "type": _ALLOC_ANNOTATE,
                "annotations": annotations,
            },
        )

    # ------------------------------------------------------------------
    # Override points
    # ------------------------------------------------------------------

    def schedule(self):
        """Called to run the scheduling loop.

        Override this in subclasses to implement allocation policy.  Triggered
        automatically after every :meth:`alloc`, :meth:`free`,
        :meth:`cancel`, :meth:`prioritize`, :meth:`expiration`, and
        :meth:`resource_update` event via an adaptive one-shot timer.  When
        events arrive in rapid bursts the timer delay grows to coalesce them
        into fewer :meth:`schedule` calls; when events are infrequent the
        delay returns to zero so that each event is processed promptly.

        **Generator protocol** — if this method returns a generator
        the base class advances it one yield per reactor iteration, allowing
        other events (new jobs, free responses, RPCs) to be handled between
        yields.
        Yield at each point where reactor responsiveness is desired — typically
        after each dispatched or denied job, but also when blocking on
        resources::

            def schedule(self):
                while self._queue:
                    job = self._queue[0]
                    try:
                        alloc = self.resources.alloc(job.jobid, job.resource_request)
                    except InsufficientResources:
                        break
                    except InfeasibleRequest as exc:
                        job.request.deny(str(exc))
                    else:
                        job.request.success(alloc)
                    heapq.heappop(self._queue)
                    yield  # hand control back to the reactor

        When a new scheduling event arrives while a generator pass is in
        progress the base class closes the generator (triggering any
        ``finally`` blocks) and starts a fresh pass after the settling delay.
        This ensures a newly submitted high-priority job or a freed resource
        is considered from the top of the queue without waiting for the
        current pass to finish.

        Non-generator ``schedule()`` implementations (no ``yield``) behave
        exactly as before and remain fully supported.
        """

    def hello(self, jobid, priority, userid, t_submit, R):
        """Called for each running job during the hello protocol.

        Invoked synchronously during :meth:`run` before the reactor starts.
        The default implementation marks *R* as allocated in the resource pool,
        which is sufficient for most schedulers.  Override to additionally
        track per-job state (e.g., store the job's expiration time).

        Args:
            jobid (int): The job ID.
            priority (int): Job priority.
            userid (int): Submitting user ID.
            t_submit (float): Job submission time.
            R: The job's current allocation.
        """
        self.resources.register_alloc(jobid, R)

    def alloc(self, request, jobid, priority, userid, t_submit, jobspec):
        """Queue an allocation request for the next :meth:`schedule` pass.

        Parses *jobspec* and pushes a :class:`PendingJob` onto
        :attr:`_queue`.  Override to change how jobs are parsed or queued.

        Args:
            request (:class:`AllocRequest`): The allocation request to respond to.
            jobid (int): The job ID (also available as ``request.jobid``).
            priority (int): Job priority.
            userid (int): Submitting user ID.
            t_submit (float): Job submission time.
            jobspec (dict): The raw jobspec dict.
        """
        try:
            rr = self.resources.parse_resource_request(jobspec)
        except (KeyError, IndexError, ValueError) as exc:
            request.deny(f"cannot parse jobspec: {exc}")
            return
        heapq.heappush(
            self._queue,
            PendingJob(jobid, priority, t_submit, request, rr),
        )

    def free(self, jobid, R, final=False):
        """Return released resources to the pool.

        Calls :meth:`~ResourcePool.free` on the resource pool.  Override
        (calling ``super().free()``) to perform additional bookkeeping such
        as removing per-job state.

        Args:
            jobid (int): The job ID.
            R: The released resources.
            final (bool): True if this is the final free for the job.
        """
        self.resources.free(jobid, R, final)

    def cancel(self, jobid):
        """Remove a pending job from the queue and respond with cancel.

        Args:
            jobid (int): The job ID to cancel.
        """
        for i, job in enumerate(self._queue):
            if job.jobid == jobid:
                self._queue.pop(i)
                heapq.heapify(self._queue)
                job.request.cancel()
                return

    def prioritize(self, jobs):
        """Apply priority updates to queued jobs and re-sort the queue.

        Args:
            jobs (list): List of ``[jobid, priority]`` pairs.
        """
        priority_map = {jobid: priority for jobid, priority in jobs}
        for job in self._queue:
            if job.jobid in priority_map:
                job.priority = priority_map[job.jobid]
        heapq.heapify(self._queue)

    def resource_update(self):
        """Called after resource state changes from resource.acquire.

        The updated state is available via :attr:`resources`.
        """

    def forecast(self):
        """Called after :meth:`schedule` to annotate pending jobs with
        forward-looking estimates.

        Override in subclasses to populate ``sched.t_estimate`` (and other
        forward-looking annotations) on pending jobs.  The base class
        implementation is a no-op.

        Triggered automatically after each :meth:`schedule` call completes.
        If a forecast pass is already in progress when a new scheduling event
        arrives, it is left running to completion; the next forecast pass
        starts after the current one finishes and the next :meth:`schedule`
        pass completes.  Forecast estimates are approximate by design, so a
        slightly stale ``t_estimate`` is preferable to having none at all.

        Supports the same **generator protocol** as :meth:`schedule`: add
        ``yield`` at each desired reactor handoff point — typically after each
        annotated job — to return control to the reactor between annotations.
        """

    def expiration(self, msg, jobid, expiration):
        """Called when job-manager requests a job expiration update.

        The default implementation accepts all updates by responding success.
        Planning schedulers that track per-job expiration should override this
        to update their internal records before calling
        ``self.handle.respond(msg, None)``.

        Args:
            msg: The request :class:`~flux.message.Message`.
            jobid (int): The job ID whose expiration is being updated.
            expiration (float): New expiration timestamp (seconds since epoch).
        """
        self.handle.respond(msg, None)

    def feasibility_check(self, msg, jobspec):
        """Called for ``feasibility.check`` RPC.

        Default implementation responds success if the job could ever fit
        within the total resource set (ignoring current availability).
        Subclasses may override for custom feasibility logic.

        Args:
            msg: The request :class:`~flux.message.Message`.
            jobspec (dict): The raw jobspec dict.
        """
        try:
            rr = self.resources.parse_resource_request(jobspec)
            self.resources.check_feasibility(rr)
        except (KeyError, IndexError, ValueError) as exc:
            self.handle.respond_error(msg, errno.EINVAL, str(exc))
            return
        except InfeasibleRequest as exc:
            self.handle.respond_error(msg, errno.EOVERFLOW, str(exc))
            return
        except OSError as exc:
            self.handle.respond_error(msg, exc.errno, str(exc))
            return
        self.handle.respond(msg, None)

    # ------------------------------------------------------------------
    # run() — initialization then reactor
    # ------------------------------------------------------------------

    def run(self):
        """Initialize the scheduler and start the reactor.

        Performs the RFC 27 initialization sequence:

        1. Register the ``sched`` dynamic service
        2. Register the ``feasibility`` dynamic service
        3. ``flux_module_set_running()`` — allow ``flux module load`` to return
        4. ``resource.acquire`` — sync first response, async continuation
        5. ``job-manager.sched-hello`` — sync hello protocol
        6. ``job-manager.sched-ready`` — announce readiness
        7. Start the reactor

        Raises:
            OSError: If any initialization step fails.
        """
        self._service_register("sched")
        self._service_register("feasibility")
        try:
            self.set_running()
            self._acquire_resources()
            self._sched_hello()
            self._sched_ready()
        except OSError as exc:
            self.log.error(f"initialization failed: {exc}")
            raise
        super().run()

    # ------------------------------------------------------------------
    # RFC 27 sched message handlers (registered by BrokerModule)
    # ------------------------------------------------------------------

    @request_handler("sched.alloc", prefix=False)
    def _handle_alloc(self, msg):
        try:
            p = msg.payload
            request = AllocRequest(self, msg)
            self.alloc(
                request,
                p["id"],
                p["priority"],
                p["userid"],
                p["t_submit"],
                p["jobspec"],
            )
            self._request_schedule()
        except Exception as exc:
            self.log.error(f"alloc callback raised: {exc}")
            self.stop_error()

    @request_handler("sched.free", prefix=False)
    def _handle_free(self, msg):
        try:
            p = msg.payload
            R = ResourcePool(p["R"])
            self.free(p["id"], R, final=p.get("final", False))
            self._request_schedule()
        except Exception as exc:
            self.log.error(f"free callback raised: {exc}")
            self.stop_error()

    @request_handler("sched.cancel", prefix=False)
    def _handle_cancel(self, msg):
        try:
            self.cancel(msg.payload["id"])
            self._request_schedule()
        except Exception as exc:
            self.log.error(f"cancel callback raised: {exc}")
            self.stop_error()

    @request_handler("sched.prioritize", prefix=False)
    def _handle_prioritize(self, msg):
        try:
            self.prioritize(msg.payload.get("jobs", []))
            self._request_schedule()
        except Exception as exc:
            self.log.error(f"prioritize callback raised: {exc}")
            self.stop_error()

    @request_handler("sched.resource-status", prefix=False)
    def _handle_resource_status(self, msg):
        try:
            pool = self._resources
            resp = {
                "all": pool.to_dict(),
                "allocated": pool.copy_allocated().to_dict(),
                "down": pool.copy_down().to_dict(),
            }
            self.handle.respond(msg, resp)
        except Exception as exc:
            self.log.error(f"resource-status failed: {exc}")
            lib.flux_respond_error(
                self.handle.handle, msg.handle, errno.EINVAL, str(exc).encode()
            )

    @request_handler("sched.expiration", prefix=False)
    def _handle_expiration(self, msg):
        try:
            p = msg.payload
            jobid = p["id"]
            exp = p["expiration"]
            if exp < 0:
                raise ValueError(f"invalid expiration {exp}")
            self.expiration(msg, jobid, exp)
            self._request_schedule()
        except Exception as exc:
            self.log.error(f"expiration callback raised: {exc}")
            lib.flux_respond_error(
                self.handle.handle, msg.handle, errno.EINVAL, str(exc).encode()
            )

    @request_handler("feasibility.check", prefix=False)
    def _handle_feasibility(self, msg):
        try:
            self.feasibility_check(msg, msg.payload.get("jobspec", {}))
        except Exception as exc:
            self.log.error(f"feasibility callback raised: {exc}")
            errmsg = str(exc).encode()
            if (
                lib.flux_respond_error(
                    self.handle.handle, msg.handle, errno.EINVAL, errmsg
                )
                < 0
            ):
                self.stop_error()

    @request_handler("stats-get")
    def _handle_stats_get(self, msg):
        self.handle.respond(msg, self.stats_get())

    def stats_get(self):
        """Return a dict of scheduler statistics for the stats-get RPC.

        Called by the built-in ``<module-name>.stats-get`` handler.
        Subclasses may override to add extra fields; call ``super().stats_get()``
        and update the returned dict:

        .. code-block:: python

            def stats_get(self):
                stats = super().stats_get()
                stats["my_counter"] = self._my_counter
                return stats

        Standard fields:

        ``sched_passes``
            Number of completed :meth:`schedule` passes.
        ``sched_yields``
            Total yields across all :meth:`schedule` generator passes
            (always 0 for synchronous schedulers).
        ``forecast_passes``
            Number of completed :meth:`forecast` passes.
        ``forecast_yields``
            Total yields across all :meth:`forecast` generator passes
            (always 0 for synchronous schedulers).
        ``sched_delay``
            Current adaptive burst-coalescing delay in seconds (0 = immediate).
            Always 0 for generator-based schedulers.
        ``sched_duration_ewma``
            EWMA of :meth:`schedule` wall-clock duration in seconds.
            Always 0 for generator-based schedulers.
        ``sched_interval_ewma``
            EWMA of time between :meth:`_request_schedule` calls in seconds.
            Tracked for all schedulers but does not affect ``sched_delay``
            for generator-based schedulers.
        ``pending_jobs``
            Current number of pending alloc requests in the scheduler queue.
        """
        return {
            "sched_passes": self._sched_passes,
            "sched_yields": self._sched_yields,
            "forecast_passes": self._forecast_passes,
            "forecast_yields": self._forecast_yields,
            "sched_delay": self._sched_delay,
            "sched_duration_ewma": self._sched_duration_ewma,
            "sched_interval_ewma": self._sched_interval_ewma,
            "pending_jobs": len(self._queue),
        }

    # ------------------------------------------------------------------
    # Internal: RFC 27 initialization helpers
    # ------------------------------------------------------------------

    def _service_register(self, name):
        """Register a dynamic service by name (synchronous)."""
        self.handle.service_register(name).get()

    def _acquire_resources(self):
        """Issue resource.acquire, process first response synchronously."""
        f = self.handle.rpc("resource.acquire", flags=FLUX_RPC_STREAMING)
        try:
            data = f.get()
        except OSError as exc:
            raise OSError(f"resource.acquire failed: {exc}") from exc

        # Build initial resource pool from the "resources" R object
        R = data.get("resources")
        if R is None:
            raise OSError("resource.acquire: missing 'resources' field")
        self._resources = ResourcePool(R, log=self.log, **self.pool_kwargs)

        # Warn once about any pool_kwargs not recognised by this pool
        # implementation, then prune them so subsequent pool constructions
        # (e.g. during the hello loop) don't repeat the warning.
        known = getattr(self._resources.impl, "known_options", frozenset())
        for key in [k for k in self.pool_kwargs if k not in known]:
            self.log.warning(f"pool: ignoring unknown option {key!r}")
            del self.pool_kwargs[key]

        # All resources start down; first update brings some up
        self._resources.mark_down("all")
        self._apply_resource_update(data)

        # Reset past the first response (mirrors flux_future_reset in
        # ss_resource_update), then install async continuation for subsequent
        # resource state changes (up/down/shrink/expiration).
        f.reset()
        self._acquire_rpc = f
        f.then(self._acquire_continuation)

    def _acquire_continuation(self, future):
        """Async continuation for streaming resource.acquire responses."""
        try:
            data = future.get()
        except OSError as exc:
            self.log.error(f"exiting due to resource update failure: {exc}")
            self.stop()
            return
        self._apply_resource_update(data)
        future.reset()

    def _apply_resource_update(self, data):
        """Update resource state from a resource.acquire response dict."""
        if "up" in data:
            self._resources.mark_up(data["up"])
        if "down" in data:
            self._resources.mark_down(data["down"])
        if "expiration" in data and data["expiration"] >= 0:
            self._resources.expiration = data["expiration"]
            self.log.info(
                f"resource expiration updated to {data['expiration']:.2f}",
            )
        if "shrink" in data:
            from flux.idset import IDset

            self._resources.remove_ranks(IDset(data["shrink"]))
        self.resource_update()
        self._request_schedule()

    def _job_raise_continuation(self, future, jobid):
        """Log an error if a job_raise RPC fails."""
        try:
            future.get()
        except OSError as exc:
            self.log.error(f"error raising fatal exception on {jobid.f58}: {exc}")

    def _sched_hello(self):
        """Synchronous hello protocol with job-manager (RFC 27).

        Sends ``job-manager.sched-hello`` and for each running job looks up
        R from the KVS and calls :meth:`hello`.
        """
        f = self.handle.rpc(
            "job-manager.sched-hello",
            {"partial-ok": self.hello_partial_ok},
            flags=FLUX_RPC_STREAMING,
        )
        while True:
            try:
                data = f.get()
            except OSError as exc:
                if exc.errno == errno.ENODATA:
                    break
                raise OSError(f"sched-hello failed: {exc}") from exc

            jobid = data["id"]
            free_ranks = data.get("free")

            # Look up R from KVS
            buf = ffi.new("char[128]")
            if lib.flux_job_kvs_key(buf, 128, jobid, b"R") < 0:
                raise OSError("flux_job_kvs_key failed")
            key = ffi.string(buf).decode()

            try:
                R_dict = kvs_get(self.handle, key)
            except OSError:
                self.log.error(f"hello: failed to look up R for job {JobID(jobid).f58}")
                f.reset()
                continue

            # If partial-ok and some ranks are free, strip them from R
            if free_ranks:
                from flux.idset import IDset

                rset = ResourcePool(R_dict, log=self.log, **self.pool_kwargs)
                rset.remove_ranks(IDset(free_ranks))
                R_dict = rset.to_dict()

            try:
                R = ResourcePool(R_dict, log=self.log, **self.pool_kwargs)
                self.hello(
                    jobid,
                    data["priority"],
                    data["userid"],
                    data["t_submit"],
                    R,
                )
            except Exception as exc:
                self.log.error(
                    f"hello callback failed for job {JobID(jobid).f58}: {exc}",
                )
                self.log.error(
                    f"raising fatal exception on running job id={JobID(jobid).f58}",
                )
                f_raise = job_raise_async(
                    self.handle,
                    jobid,
                    "scheduler-restart",
                    0,
                    "failed to reallocate R for running job",
                )
                f_raise.then(self._job_raise_continuation, JobID(jobid))

            f.reset()

    def _sched_ready(self):
        """Send job-manager.sched-ready and announce scheduler mode."""
        depth = self.queue_depth
        if depth == "unlimited":
            payload = {"mode": "unlimited"}
        else:
            try:
                n = int(depth)
                if n < 1:
                    raise ValueError
            except (TypeError, ValueError):
                raise ValueError(
                    f"queue_depth must be a positive integer or 'unlimited', "
                    f"got {depth!r}"
                )
            payload = {"mode": "limited", "limit": n}

        f = self.handle.rpc("job-manager.sched-ready", payload)
        try:
            f.get()
        except OSError as exc:
            raise OSError(f"sched-ready failed: {exc}") from exc

        self.log.info(
            f"ready: queue-depth={depth}"
            f" log-level={self.log.level_name}"
            f" Rv{self.resources.version}"
            f" {self.resources.dumps()}",
        )


# vi: ts=4 sw=4 expandtab
