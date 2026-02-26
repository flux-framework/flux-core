###############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Abstract classes for journal consumer interfaces"""

import errno
import time
from abc import ABC, abstractmethod
from collections import deque

from flux.constants import FLUX_RPC_NORESPONSE, FLUX_RPC_STREAMING
from flux.eventlog import EventLogEvent


class JournalEventBase(EventLogEvent):
    """A container for an event from a journal RPC interface

    Attributes:
        name (str): event name (Possible event names will depend on the
            journal service being consumed)
        timestamp (float): event timestamp in seconds since the epoch
            with sub-millisecond precision.
        context (dict): context dictionary
        context_string (str): context dict converted to comma separated
            key=value string.
    """

    def __init__(self, event):
        # If no timestamp, then capture the time now
        if "timestamp" not in event:
            event["timestamp"] = time.time()
        super().__init__(event)

    def is_empty(self):
        """Return True if this event is an empty journal event"""
        return "name" not in self

    def __str__(self):
        if self.is_empty():
            return f"{self.timestamp:<0.5f} End of historical data stream"
        return " ".join(
            [
                f"{self.timestamp:<0.5f}",
                self.name,
                self.context_string,
            ]
        )


class JournalConsumerBase(ABC):
    """Base class for consuming events from a journal-style RPC

    This base class implements a wrapper around journal style RPCs
    which allow clients to obtain a stream of events, possibly including
    historical data.

    Subclasses of :obj`JournalConsumerBase` should be implemented to more
    completely handle the specifics of a given interface, but all such
    subclasses will follow the interface documented here.

    A journal consumer is created by passing the constructor an open
    :obj:`~flux.Flux` handle, the topic string of the journal service along
    with any other optional parameters described below. The :func:`start`
    method should then be called, which sends the initial RPC. To stop the
    stream of events, call the :func:`stop` method. After all events have
    been processed :func:`poll` or the registered callback will return None.

    When the consumer is first started, historical data (events in the past)
    will be sent from the journal service unless *full* is set to False.
    These events are stored until all historical events are processed,
    then are time ordered before returning them via :func:`poll` or to
    the callback registered by :func:`set_callback`.

    To avoid processing previously seen events with a new instance of this
    class, the timestamp of the newest processed event can be passed via the
    *since* parameter. Timestamps should be unique so :func:`poll` or the
    callback will start with the newest event after the *since* timestamp.

    When *full* is True, a compliant journal service sends a sentinel
    event in the journal stream to demarcate the transition between
    history and current events. If *include_sentinel* is set to True,
    then an empty event will be returned by :func:`poll` or the
    registered callback to represent the sentinel. An empty event
    can be compared to :data:`self.SENTINEL_EVENT` or by using the
    :func:`JournalEventBase.is_empty` method. The default behavior is to
    suppress the sentinel.

    Subclasses of :obj:`JournalConsumerBase` must implement the following
    abstract methods, see method documentation for details:

      * :meth:`flux.abc.JournalConsumerBase.process_response`, in which a
        single response from the journal service is processed and a dictionary
        of kwargs is returned.
      * :meth:`flux.abc.JournalConsumerBase.create_event`, which is passed
        one event entry from the same response and the `**kwargs` obtained
        from above, and should return an object of a class derived from
         :obj:`JournalEventBase`

    Subclasses may optionally implement the following methods:

      * :meth:`flux.abc.JournalConsumerBase.is_empty_response`, which is
        used to determine if a journal response message is to be considered
        the "empty" sentinel response.

    Finally, subclasses may optionally override the following properties:

      * :data:`request_payload`, which is the payload of a journal request
        (the default is an empty payload)

      * :data:`SENTINEL_EVENT`, which is a class constant representing the
        sentinel event for this class.

    """

    SENTINEL_EVENT = JournalEventBase({})

    def __init__(
        self,
        flux_handle,
        topic,
        cancel_topic=None,
        full=True,
        since=0.0,
        include_sentinel=False,
    ):
        """Initialize a :obj:`JournalConsumerBase` instance

        Args:
            flux_handle (:obj:`~flux.Flux`): A Flux handle
            topic (str): The journal RPC topic string
            cancel_topic (:obj:`str`, optional): The RPC topic string to cancel
                an active journal request. If not set, it will defuault to
                topic + "-cancel".
            full (bool, optional): Whether to return full history, if the
                journal interface supports it. Defaults to True.
            since (float, optional): Return only events that have a timestamp
                greater than ``since``.
            include_sentinel (bool, optional): Return an empty event upon
                receipt of the sentinel from the journal service. The default
                is to suppress this event.
        """
        self.handle = flux_handle
        self.topic = topic
        self.cancel_topic = cancel_topic

        if self.cancel_topic is None:
            self.cancel_topic = f"{self.topic}-cancel"

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
            self.backlog.append(self.SENTINEL_EVENT)

    @abstractmethod
    def process_response(self, resp):
        """Process a journal response.

        This method may return a dictionary which is then passed as
        ``*kwargs`` to :meth:`create_event` below.

        Args:
            resp (dict): The payload of a single journal response (converted
                from json to a dict)
        Returns:
            dict
        """
        return {}

    @abstractmethod
    def create_event(self, entry, **kwargs):
        """Return a journal event object from a response entry

        This method should return an event object given an individual eventlog
        entry and ``**kwargs`` returned from :meth:`process_response`.

        Args:
            entry (dict): An individual event dictionary from the ``events``
                array of a journal response.
            **kwargs : Additional keyword arguments returned from the
                :meth:`process_response` method.

        Returns:
            :obj:`JournalEventBase` or a subclass

        """
        return JournalEventBase(entry)

    def __enqueue_response(self, resp, *args):
        if resp is None:
            # End of data, enqueue None:
            self.backlog.append(None)
            return

        kwargs = self.process_response(resp)
        for entry in resp["events"]:
            event = self.create_event(entry, **kwargs)
            if event.timestamp > self.since:
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

    def is_empty_response(self, resp):
        """Return True if resp is "empty" """
        return len(resp["events"]) == 0

    @property
    def request_payload(self):
        """Appropriate request payload for this journal RPC"""
        return {}

    def start(self):
        """Start the stream of events by sending a request

        This function initiates the stream of events for a journal
        consumer by sending the initial request to the configured journal
        service endpoint.

        .. note::
            If :func:`start` is called more than once the stream of events
            will be restarted using the original options passed to the
            constructor. This may cause duplicate events, or missed events
            if *full* is False since no history will be included.
        """
        self.rpc = self.handle.rpc(
            self.topic, self.request_payload, 0, FLUX_RPC_STREAMING
        )
        #  Need to call self.rpc.then() if a user cb is registered:
        self.__set_then_cb()

        return self

    def stop(self):
        """Cancel the journal RPC

        Cancel the RPC. This will eventually stop the stream of events to
        either :func:`poll` or the defined callback. After all events have
        been processed an *event* of None will be returned by :func:`poll`
        or the defined callback.
        """
        self.handle.rpc(
            self.cancel_topic,
            {"matchtag": self.rpc.pimpl.get_matchtag()},
            0,
            FLUX_RPC_NORESPONSE,
        )

    def poll(self, timeout=-1.0):
        """Synchronously get the next journal event

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
                if self.is_empty_response(resp):
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
            #  processing history so that those events can be sorted in
            #  timestamp order:
            return
        while self.backlog:
            self.cb(self.__next_event(), *self.cb_args, **self.cb_kwargs)

    def __cb(self, future):
        try:
            resp = future.get()
            if self.processing_inactive and self.is_empty_response(resp):
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
        """Register callback *event_cb* to be called for each journal event

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
