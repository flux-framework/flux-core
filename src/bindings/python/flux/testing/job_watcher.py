###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Job event watching for testing infrastructure.

Defines :class:`JobEventWatcher`, an abstract interface for delivering
main-eventlog events to a callback, and two concrete implementations:

  * :class:`JournalEventWatcher` wraps :class:`flux.job.JournalConsumer`,
    obtaining events for every job in the instance via a single
    subscription and filtering to the registered jobid set.

  * :class:`PerJobEventWatcher` opens one
    :func:`flux.job.event_watch_async` future per registered jobid.
"""

import time
from abc import ABC, abstractmethod

import flux.job
from flux.job.journal import JournalConsumer


class JobEventWatcher(ABC):
    """Abstract interface for delivering job events to a callback.

    Implementations deliver main-eventlog events for explicitly-tracked
    jobids. The contract is that ``on_event(jobid, event)`` is invoked
    on the reactor thread for each main-eventlog event on each
    registered jobid.

    Lifecycle:

      1. Construct with a Flux handle.
      2. :meth:`add_job` registers a jobid to be tracked. May be
         called before or after :meth:`start`. Events received for
         a jobid before its registration are queued and delivered
         when :meth:`add_job` is called for that jobid.
      3. :meth:`start` begins the event stream.
      4. :meth:`stop` releases watcher resources. The caller drives
         the reactor until it exits naturally.

    Event objects passed to ``on_event`` have at minimum:

      * ``name`` (str)
      * ``timestamp`` (float, epoch seconds)
      * ``context`` (dict)

    Implementations may additionally populate ``R`` and ``jobspec``
    attributes when the underlying source provides them inline.
    Callers should access these via :func:`getattr` with a default of
    ``None``.
    """

    # Future work: a JobWatcher-backed implementation that also
    # forwards output and guest.exec eventlog events would fit this
    # interface unchanged — it would track a jobid set and filter to
    # main-eventlog events (or extend the contract to expose
    # additional eventlogs).

    def __init__(self, handle):
        self.handle = handle

    @abstractmethod
    def add_job(self, jobid):
        """Register a jobid to be tracked. Events for this jobid will
        be delivered to the ``on_event`` callback set in :meth:`start`.
        """

    @abstractmethod
    def start(self, on_event):
        """Start the watcher. ``on_event(jobid, event)`` is invoked
        on the reactor thread for every main-eventlog event on every
        tracked jobid.
        """

    @abstractmethod
    def stop(self):
        """Stop the watcher. Drains in-flight events; the caller must
        still drive the reactor until it exits naturally.
        """


class JournalEventWatcher(JobEventWatcher):
    """:class:`JobEventWatcher` backed by the job-manager events journal.

    A single :class:`flux.job.JournalConsumer` subscription delivers
    events for every job in the instance; events for unregistered
    jobids are filtered out in an internal callback.

    The consumer is started with ``full=True`` and ``since`` set to
    the watcher's construction time. This catches events that occur
    between consumer construction and the subscription becoming
    active, while filtering out unrelated history. ``include_sentinel``
    is left at its default of ``False`` so the historical-to-live
    transition is transparent to callers.

    add_job-after-start is supported. Events that arrive for a
    jobid before :meth:`add_job` registers it are buffered and
    replayed on the registering call, in arrival order. This
    closes a race intrinsic to the journal-plus-async-submit pattern:
    the journal can deliver a submit event before the submit RPC's
    response has reached the caller's callback.

    Warning: the internal buffer (``_buffered``) is unbounded. In a
    shared Flux instance where other users are also submitting jobs,
    events for those foreign jobids will accumulate in the buffer for
    the lifetime of the watcher. Use :class:`PerJobEventWatcher` in
    that case, or ensure this watcher is used in an isolated instance.
    """

    # Future work: cap the per-watcher buffer size for use in shared
    # Flux instances, where events for jobids that are never
    # registered (because they belong to other producers) would
    # otherwise accumulate.

    def __init__(self, handle):
        super().__init__(handle)
        self._tracked = set()
        self._consumer = None
        self._on_event = None
        self._since = time.time()
        # Per-jobid buffers for events that arrived before add_job()
        # registered the jobid. Drained on add_job() in arrival order.
        self._buffered = {}

    def add_job(self, jobid):
        jobid = int(jobid)
        self._tracked.add(jobid)
        # Replay any events that arrived for this jobid before
        # registration. Replay happens on the calling thread.
        for event in self._buffered.pop(jobid, ()):
            self._on_event(event.jobid, event)

    def start(self, on_event):
        self._on_event = on_event
        self._consumer = JournalConsumer(self.handle, full=True, since=self._since)
        self._consumer.set_callback(self._journal_cb)
        self._consumer.start()

    def stop(self):
        if self._consumer is not None:
            self._consumer.stop()

    def _journal_cb(self, event):
        if event is None:
            # End-of-stream after stop(); no further events arrive.
            return
        if event.is_empty():
            # Sentinel marking historical-to-live transition.
            # Suppressed by include_sentinel=False; ignore defensively.
            return
        jobid = int(event.jobid)
        if jobid in self._tracked:
            self._on_event(event.jobid, event)
        else:
            self._buffered.setdefault(jobid, []).append(event)


class PerJobEventWatcher(JobEventWatcher):
    """:class:`JobEventWatcher` backed by per-job ``event_watch_async``.

    One :func:`flux.job.event_watch_async` future is opened per
    registered jobid. Each future fires its callback once per event
    until end-of-stream, after which the future is reset.

    Events returned by ``event_watch_async`` are
    :class:`~flux.eventlog.EventLogEvent`-shaped (``name``,
    ``timestamp``, ``context``); they do not carry inline ``R`` or
    ``jobspec``. Callers needing those should fall back to
    :func:`flux.job.kvs_lookup`.
    """

    def __init__(self, handle):
        super().__init__(handle)
        self._on_event = None
        self._futures = {}  # jobid -> future
        self._pending_jobs = []  # jobids added before start()

    def add_job(self, jobid):
        jobid = int(jobid)
        if self._on_event is None:
            self._pending_jobs.append(jobid)
        else:
            self._start_watch(jobid)

    def start(self, on_event):
        self._on_event = on_event
        for jobid in self._pending_jobs:
            self._start_watch(jobid)
        self._pending_jobs = []

    def stop(self):
        """No-op: per-job futures complete naturally when each job's
        eventlog ends.

        Warning: unlike :class:`JournalEventWatcher`, this method does
        not preempt in-flight watches. If jobs are still active when
        ``stop()`` is called, their event-watch futures remain active
        until those jobs reach a terminal state. Callers that need to
        stop promptly should cancel all tracked jobs first (e.g. via
        :meth:`BulkRun.cancelall`) and wait for their ``clean`` events.
        """

    def _start_watch(self, jobid):
        future = flux.job.event_watch_async(self.handle, jobid)
        future.then(self._event_cb, jobid)
        self._futures[jobid] = future

    def _event_cb(self, future, jobid):
        try:
            event = future.get_event()
        except OSError:
            event = None
        if event is None:
            self._futures.pop(jobid, None)
            future.reset()
            return
        self._on_event(jobid, event)
        future.reset()


def default_watcher_factory(handle):
    """Return the default :class:`JobEventWatcher` implementation.

    :class:`JournalEventWatcher` is preferred: it imposes a single
    subscription on ``kvs-watch`` regardless of jobid count, and
    exposes inline ``R`` and ``jobspec`` data on the relevant events.
    """
    return JournalEventWatcher(handle)


# vi: ts=4 sw=4 expandtab
