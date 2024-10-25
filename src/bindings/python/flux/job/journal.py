###############################################################
# Copyright 2024 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import errno
from collections import deque

from flux.constants import FLUX_RPC_NORESPONSE, FLUX_RPC_STREAMING
from flux.job import JobID
from flux.job.event import EventLogEvent


class JournalEvent(EventLogEvent):
    """A container for an event from the job manager journal

    Attributes:
        jobid (:obj:`flux.job.JobID`): The job id for which the event applies
        name (str): event name (See :rfc:`21` for possible event names)
        timestamp (float): event timestamp in seconds since the epoch
            with sub-millisecond precision.
        context (dict): context dictionary
            (See `RFC 21: Event Descriptions <https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_21.html#event-descriptions>`_.)
        context_string (str): context dict converted to comma separated
            key=value string.
        jobspec (dict): For ``submit`` events, the job's redacted jobspec
            (See :rfc:`25`).
        R (dict): For alloc events, the job's assigned R (See :rfc:`20`)
    """

    def __init__(self, jobid, event, jobspec=None, R=None):
        super().__init__(event)
        self.jobid = JobID(jobid)
        self.jobspec = jobspec
        self.R = R

    def is_empty(self):
        """Return True if this event is an empty journal event"""
        if self.jobid == -1:
            return True
        return False

    def __str__(self):
        if self.is_empty():
            return "-1: End of historical data stream"
        return " ".join(
            [
                f"{self.jobid.f58}:",
                f"{self.timestamp:<0.5f}",
                self.name,
                self.context_string,
            ]
        )


SENTINEL_EVENT = JournalEvent(-1, {})
"""
A constant :obj:`JournalEvent` demarcating the transition from historical
events to current events when a :obj:`JournalConsumer` is created with
*include_sentinel* set to True.
"""


class JournalConsumer:
    """Class for consuming the job manager journal

    This class is a wrapper around the ``job-manager.events-journal`` RPC,
    which allows clients to subscribe to primary job events for all jobs
    (See :rfc:`21`) in a single interface.

    A :obj:`JournalConsumer` returns individual job events
    as :obj:`JournalEvent` objects either synchronously, via the
    :func:`poll` method, or asynchronously via a callback registered with
    the :func:`set_callback` method. In the case of asynchronous mode, the
    Flux reactor must be run via :func:`~flux.core.handle.Flux.reactor_run`.

    A :obj:`JournalConsumer` is created by passing the constructor an open
    :obj:`~flux.Flux` handle, along with any other optional parameters
    described below. The :func:`start` method should then be called,
    which sends the initial RPC. To stop the stream of events, call the
    :func:`stop` method. After all events have been processed :func:`poll`
    or the registered callback will return None.

    When the consumer is first started, historical data (events in the
    past) will be sent from the job manager unless *full* is set to False.
    These events are stored until all historical events are processed,
    then are time ordered before returning them via :func:`poll` or to
    the callback registered by :func:`set_callback`.

    To avoid processing previously seen events with a new instance of this
    class, the timestamp of the newest processed event can be passed via the
    *since* parameter. Timestamps should be unique so :func:`poll` or the
    callback will start with the newest event after the *since* timestamp.

    When *full* is True, the job manager sends a sentinel event in
    the journal stream to demarcate the transition between history
    and current events. If *include_sentinel* is set to True,
    then an empty event will be returned by :func:`poll` or the
    registered callback to represent the sentinel. An empty event can
    be compared to :data:`flux.job.journal.SENTINEL_EVENT` or by using the
    :func:`JournalEvent.is_empty` method. The default behavior is to
    suppress the sentinel.

    """

    def __init__(self, flux_handle, full=True, since=0.0, include_sentinel=False):
        self.handle = flux_handle
        self.backlog = deque()
        self.full = full
        self.since = since
        self.rpc = None
        self.cb = None
        self.cb_args = []
        self.cb_kwargs = {}
        self.processing_inactive = self.full
        self.include_sentinel = include_sentinel

    def __sort_backlog(self):
        self.processing_inactive = False
        self.backlog = deque(sorted(self.backlog, key=lambda x: x.timestamp))
        if self.include_sentinel:
            self.backlog.append(SENTINEL_EVENT)

    def __enqueue_response(self, resp):
        if resp is None:
            # End of data, enqueue None:
            self.backlog.append(None)
            return

        jobid = resp["id"]
        jobspec = resp.get("jobspec")
        R = resp.get("R")
        for entry in resp["events"]:
            event = JournalEvent(jobid, entry)
            if event.timestamp > self.since:
                if event.name == "submit":
                    event.jobspec = jobspec or None
                elif event.name == "alloc":
                    event.R = R or None
                self.backlog.append(event)

    def __next_event(self):
        return self.backlog.popleft()

    def __set_then_cb(self):
        if self.rpc is not None and self.cb is not None:
            try:
                self.rpc.then(self.__cb)
            except OSError as exc:
                if exc.errno == errno.EINVAL:
                    # then callback is already set
                    pass

    def start(self):
        """Start the stream of events by sending a request to the job manager

        This function sends the job-manager.events-journal RPC to the
        job manager. It must be called to start the stream of events.

        .. note::
            If :func:`start` is called more than once the stream of events
            will be restarted using the original options passed to the
            constructor. This may cause duplicate events, or missed events
            if *full* is False since no history will be included.
        """
        self.rpc = self.handle.rpc(
            "job-manager.events-journal", {"full": self.full}, 0, FLUX_RPC_STREAMING
        )
        #  Need to call self.rpc.then() if a user cb is registered:
        self.__set_then_cb()

        return self

    def stop(self):
        """Cancel the job-manager.events-journal RPC

        Cancel the RPC. This will eventually stop the stream of events to
        either :func:`poll` or the defined callback. After all events have
        been processed an *event* of None will be returned by :func:`poll`
        or the defined callback.
        """
        self.handle.rpc(
            "job-manager.events-journal-cancel",
            {"matchtag": self.rpc.pimpl.get_matchtag()},
            0,
            FLUX_RPC_NORESPONSE,
        )

    def poll(self, timeout=-1.0):
        """Synchronously get the next job event

        if *full* is True, then this call will not return until all
        historical events have been processed. Historical events will sorted
        in time order and returned once per :func:`poll` call.

        :func:`start` must be called before this function.

        Args:
            timeout (float): Only wait *timeout* seconds for the next event.
                If the timeout expires then a :exc:`TimeoutError` is raised.
                A *timeout* of -1.0 disables any timeout.
        Raises:
            RuntimeError:  :func:`poll` was called before :func:`start`.
        """
        if self.rpc is None:
            raise RuntimeError("poll() called before start()")

        if self.processing_inactive:
            # process backlog. Time order events once done:
            while self.processing_inactive:
                resp = self.rpc.wait_for(timeout).get()
                if resp["id"] == -1:
                    self.__sort_backlog()
                else:
                    self.__enqueue_response(resp)
                self.rpc.reset()

        while not self.backlog:
            try:
                resp = self.rpc.wait_for(timeout).get()
            except OSError as exc:
                if exc.errno != errno.ENODATA:
                    raise
                resp = None
            self.__enqueue_response(resp)
            self.rpc.reset()

        return self.__next_event()

    def __user_cb_flush(self):
        if self.processing_inactive:
            #  all events are accumulated in the backlog while we're still
            #  processing inactive job events so that those events can be
            #  sorted in timestamp order:
            return
        while self.backlog:
            self.cb(self.__next_event(), *self.cb_args, **self.cb_kwargs)

    def __cb(self, future):
        try:
            resp = future.get()
            if self.processing_inactive and resp["id"] == -1:
                self.__sort_backlog()
            else:
                self.__enqueue_response(resp)
            self.__user_cb_flush()
        except OSError as exc:
            if exc.errno == errno.ENODATA:
                self.__enqueue_response(None)
        finally:
            future.reset()

    def set_callback(self, event_cb, *args, **kwargs):
        """Register callback *event_cb* to be called for each job event

        If provided, ``*args``, and ``**kwargs`` are passed along to
        *event_cb*, whose only required argument is an *event*, e.g.

        >>> def event_cb(event)
        >>>     # do something with event

        After a :obj:`JournalConsumer` is stopped and the final event is
        received, *event_cb* will be called with an *event* of None, which
        signals the end of the event stream.
        """
        self.cb = event_cb
        self.cb_args = args
        self.cb_kwargs = kwargs
        self.__set_then_cb()
        return self


# vi: ts=4 sw=4 expandtab
