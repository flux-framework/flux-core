###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Bulk job submission and event observation.

Submit one or more batches of jobs against a Flux instance, observe
their main-eventlog events via a pluggable :class:`JobEventWatcher`,
and return aggregate measurements as a :class:`BulkRunResult`.
"""

import os
import time
from collections import defaultdict

import flux.job
from flux.testing.job_watcher import default_watcher_factory

_TERMINAL_EVENT = "clean"


class BulkRunResult:
    """Aggregate result of a :class:`BulkRun` execution.

    Attributes:
        jobs: mapping of jobid (int) to a dict of ``{event_name: event}``
            for the events listed in the run's ``events_of_interest``.
            When more than one batch was pushed, each per-jobid dict
            also contains a ``"_batch"`` key whose value is the batch
            index returned by :meth:`BulkRun.push_jobs`.
        script_runtime (float): Wall time of :meth:`BulkRun.run`,
            measured with :func:`time.monotonic`.
        submit_attempted (int): Total number of submissions attempted
            (sum of ``count`` across all :meth:`BulkRun.push_jobs`
            calls).
        submit_failures (list[dict]): One entry per submit RPC that
            failed (the broker rejected the request). Each entry has
            keys ``"batch"`` (the batch index), ``"submit_index"``
            (the global per-submission index, 0-based across all
            batches), and ``"error"`` (the exception message as a
            string). Failed submits do not appear in :attr:`jobs` —
            they have no jobid and produce no events — but the
            count is preserved here so callers can surface
            submission-time errors.
    """

    def __init__(self, jobs, script_runtime, submit_attempted, submit_failures):
        self.jobs = jobs
        self.script_runtime = script_runtime
        self.submit_attempted = submit_attempted
        self.submit_failures = submit_failures

    @property
    def njobs(self):
        """Number of successfully-submitted jobs in this result."""
        return len(self.jobs)

    def first_event_t(self, name):
        """Earliest broker timestamp recorded across all jobs for an event.

        Raises:
            ValueError: if no job recorded an event of this name.
        """
        ts = [j[name].timestamp for j in self.jobs.values() if name in j]
        if not ts:
            raise ValueError("no job recorded event {0!r}".format(name))
        return min(ts)

    def last_event_t(self, name):
        """Latest broker timestamp recorded across all jobs for an event.

        Raises:
            ValueError: if no job recorded an event of this name.
        """
        ts = [j[name].timestamp for j in self.jobs.values() if name in j]
        if not ts:
            raise ValueError("no job recorded event {0!r}".format(name))
        return max(ts)

    def jobids_with(self, name):
        """List of jobids that recorded an event of the given name."""
        return [jid for jid, evs in self.jobs.items() if name in evs]


class BulkRun:
    """Submit a batch of jobs, observe their lifecycle, return aggregates.

    Submit-and-react-and-summarize: queue one or more batches via
    :meth:`push_jobs`, then call :meth:`run` to burst-submit them all
    and drive the reactor until every submitted job reaches its
    terminal event.

    The :class:`JobEventWatcher` used to observe events is pluggable
    via the ``watcher_factory`` constructor argument; the default
    factory returns a :class:`JournalEventWatcher`.

    Callback hooks let callers wire phase transitions and streaming
    inspection without subclassing:

      * :meth:`add_event_cb` — invoked for every event on every
        tracked job.
      * :meth:`add_bulk_event_cb` — invoked once when every
        successfully-submitted job has seen a named event.

    Reactor termination paths:

      1. Default — all successfully-submitted jobs reach ``clean``.
         RFC 21 guarantees that ``clean`` is the last event for every
         job (including exception-terminated ones), so this path
         covers all normal cases.
      2. Explicit — a callback calls :meth:`stop`. Used when a
         caller has decided the run is complete before terminal
         events arrive naturally.

    Submit failures (broker rejection of a submit RPC) are recorded
    in :attr:`BulkRunResult.submit_failures` rather than raised; the
    failing jobids never appear in :attr:`BulkRunResult.jobs` and do
    not affect termination accounting. Callers should inspect
    ``submit_failures`` and surface a warning when non-empty.

    Time semantics follow the convention in
    ``src/test/throughput.py``:

      * Script-side timing uses :func:`time.monotonic`
        (:attr:`BulkRunResult.script_runtime`).
      * Broker-side timing uses each event's ``timestamp`` field,
        accessible via :meth:`BulkRunResult.first_event_t` and
        :meth:`BulkRunResult.last_event_t`.

    Args:
        handle: an open :class:`flux.Flux` handle.
        events_of_interest: iterable of event names to record per
            jobid. Other events still trigger callbacks but are not
            stored in the result.
        watcher_factory: callable taking a Flux handle and returning
            a :class:`JobEventWatcher`.
    """

    def __init__(
        self,
        handle,
        *,
        events_of_interest=("submit", "alloc", "clean"),
        watcher_factory=default_watcher_factory,
    ):
        self.handle = handle
        self._events_of_interest = set(events_of_interest)
        self._watcher = watcher_factory(handle)

        # Configuration accumulated before run().
        self._batches = []  # list of (jobspec_str, count)
        self._total_pushed = 0  # sum of all batch counts

        # State accumulated during run().
        self._jobs = {}  # jobid -> {event_name: event}
        self._jobid_to_batch = {}  # jobid -> batch index
        self._submits_completed = 0  # submit RPC responses received
        self._submit_failures = []  # list of {"batch": idx, "error": str}
        self._terminal_jobids = set()
        self._stop_requested = False
        self._running = False

        # Callbacks.
        self._per_event_cbs = []
        self._bulk_event_cbs = defaultdict(list)
        self._seen_per_event = defaultdict(set)
        self._bulk_fired = set()

    # -- Configuration (before run()) ----------------------------------

    def push_jobs(self, jobspec, count=1):
        """Queue ``count`` copies of ``jobspec`` for submission.

        Multiple calls before :meth:`run` queue heterogeneous batches
        (for example, a mix of small and large jobspecs) which are
        all submitted in a single :meth:`run` invocation.

        Args:
            jobspec: a :class:`flux.job.JobspecV1` (or any object with
                a ``dumps()`` method returning a JSON string), or a
                pre-serialized JSON string. The jobspec is serialized
                once here, not per-submission.
            count: number of copies to submit.

        Returns:
            int: the batch index (0 for the first call, 1 for the
            second, ...). Per-jobid records in
            :attr:`BulkRunResult.jobs` carry a ``"_batch"`` key with
            this value when more than one batch was pushed.

        Raises:
            RuntimeError: if called after :meth:`run` has begun.
            ValueError: if ``count`` is not a positive integer.
        """
        if self._running:
            raise RuntimeError("push_jobs() forbidden after run()")
        if not isinstance(count, int) or count < 1:
            raise ValueError(
                "count must be a positive integer, got {0!r}".format(count)
            )
        if hasattr(jobspec, "dumps"):
            spec_str = jobspec.dumps()
        else:
            spec_str = jobspec
        idx = len(self._batches)
        self._batches.append((spec_str, count))
        self._total_pushed += count
        return idx

    def add_event_cb(self, cb):
        """Register a per-event callback.

        ``cb(bulk_run, jobid, event)`` is invoked on the reactor
        thread for every event on every tracked job, regardless of
        whether the event name is in ``events_of_interest``.
        """
        self._per_event_cbs.append(cb)

    def add_bulk_event_cb(self, event_name, cb):
        """Register a bulk-event callback for ``event_name``.

        ``cb(bulk_run)`` is invoked on the reactor thread once when
        every successfully-submitted job has seen at least one event
        of name ``event_name``. Multiple callbacks may be registered
        for the same event name; all fire when the threshold is met.
        Each ``event_name`` fires at most once per run.
        """
        self._bulk_event_cbs[event_name].append(cb)

    # -- Reactor / job control (during run()) --------------------------

    def stop(self):
        """Request reactor exit.

        Schedules an exit from :meth:`run`'s reactor loop. The
        currently-executing callback finishes; subsequent in-flight
        callbacks may still run before the reactor returns.
        """
        if not self._stop_requested:
            self._stop_requested = True
            self.handle.reactor_stop()

    def cancelall(self):
        """Cancel every job owned by the current user.

        Sends a single ``job-manager.raiseall`` RPC with a cancel
        exception. Faster than per-jobid cancellation at scale,
        but cancels *all* jobs owned by the user — relies on the
        caller being the only producer of jobs in this Flux
        instance.

        Synchronous: blocks until the broker acknowledges the
        request. Safe to call from within a reactor callback;
        the flux reactor supports nested invocation while a
        future waits for its response.

        Does not wait for resulting ``clean`` events; callers
        that need to observe cancellation completion should
        register an :meth:`add_bulk_event_cb` for ``"clean"``.
        """
        self.handle.rpc(
            "job-manager.raiseall",
            {
                "type": "cancel",
                "severity": 0,
                "userid": os.getuid(),
                "states": 0xFF,
                "dry_run": False,
            },
        ).get()

    # -- Execution -----------------------------------------------------

    def run(self):
        """Submit queued batches and drive the reactor to completion.

        Returns:
            :class:`BulkRunResult`

        Raises:
            RuntimeError: if no batches have been queued, or if
                :meth:`run` has already been called on this instance.
        """
        if self._running:
            raise RuntimeError("run() called twice on the same BulkRun")
        if not self._batches:
            raise RuntimeError("run() called with no batches queued")
        self._running = True

        t_start = time.monotonic()

        # Start the watcher before submitting. add_job-after-start
        # is safe: JournalEventWatcher buffers events for jobids it
        # hasn't yet been told about, and the per-job watcher only
        # ever sees events for jobids we've registered.
        self._watcher.start(self._on_event)

        # Burst-submit every queued batch. submit_idx counts each
        # individual submission globally (0-based, across all
        # batches) so per-submission failures can be reported as
        # an idset rather than 5000 identical lines.
        submit_idx = 0
        for batch_idx, (spec_str, count) in enumerate(self._batches):
            for _ in range(count):
                future = flux.job.submit_async(self.handle, spec_str)
                future.then(self._submit_cb, (batch_idx, submit_idx))
                submit_idx += 1

        self.handle.reactor_run()

        self._watcher.stop()

        t_end = time.monotonic()

        # If multiple batches were pushed, annotate per-jobid records
        # with their batch index. Single-batch runs leave the dicts
        # un-cluttered.
        if len(self._batches) > 1:
            for jobid, evs in self._jobs.items():
                evs["_batch"] = self._jobid_to_batch[jobid]

        return BulkRunResult(
            jobs=self._jobs,
            script_runtime=t_end - t_start,
            submit_attempted=self._total_pushed,
            submit_failures=self._submit_failures,
        )

    # -- Internal callbacks --------------------------------------------

    def _submit_cb(self, future, info):
        """Invoked when a submit RPC response arrives.

        ``info`` is a (batch_idx, submit_idx) tuple packed in
        :meth:`run`'s submission loop. submit_idx is the global
        per-submission index (0-based, across all batches).
        """
        batch_idx, submit_idx = info
        self._submits_completed += 1
        try:
            jobid = int(future.get_id())
        except OSError as exc:
            # The broker rejected this submit. No jobid was assigned
            # and no events will arrive for this entry; record the
            # failure so the caller can surface it, then check whether
            # this completed the last outstanding submit.
            self._submit_failures.append(
                {
                    "batch": batch_idx,
                    "submit_index": submit_idx,
                    "error": str(exc),
                }
            )
            self._maybe_terminate()
            return
        self._jobs[jobid] = {}
        self._jobid_to_batch[jobid] = batch_idx
        self._watcher.add_job(jobid)

        # Re-check bulk-event firing: completing submits is a
        # precondition, and this submit may have been the last one.
        for name in list(self._bulk_event_cbs):
            self._maybe_fire_bulk(name)
        self._maybe_terminate()

    def _on_event(self, jobid, event):
        """Invoked by the watcher for each event on each tracked job."""
        jobid = int(jobid)

        # Per-event callbacks fire for every event.
        for cb in self._per_event_cbs:
            cb(self, jobid, event)

        # Record event if of interest. self._jobs[jobid] is always present
        # here: _submit_cb populates it before calling watcher.add_job(),
        # and the watcher only delivers events for registered jobids.
        if event.name in self._events_of_interest:
            self._jobs[jobid][event.name] = event

        # Bulk-event accounting: track the first occurrence of each
        # event name per jobid.
        if jobid not in self._seen_per_event[event.name]:
            self._seen_per_event[event.name].add(jobid)
            self._maybe_fire_bulk(event.name)

        # Terminal accounting.
        if event.name == _TERMINAL_EVENT and jobid not in self._terminal_jobids:
            self._terminal_jobids.add(jobid)
            self._maybe_terminate()

    def _maybe_fire_bulk(self, event_name):
        """Fire the bulk-event callback for ``event_name`` if its
        preconditions are met."""
        if event_name in self._bulk_fired:
            return
        if event_name not in self._bulk_event_cbs:
            return
        if not self._all_submits_done():
            return
        if len(self._seen_per_event[event_name]) < len(self._jobs):
            return
        self._bulk_fired.add(event_name)
        for cb in list(self._bulk_event_cbs[event_name]):
            cb(self)

    def _maybe_terminate(self):
        """Stop the reactor if every successfully-submitted job has
        reached its terminal event."""
        if self._stop_requested:
            return
        if not self._all_submits_done():
            return
        if len(self._terminal_jobids) < len(self._jobs):
            return
        self._stop_requested = True
        self.handle.reactor_stop()

    def _all_submits_done(self):
        return self._submits_completed >= self._total_pushed


# vi: ts=4 sw=4 expandtab
