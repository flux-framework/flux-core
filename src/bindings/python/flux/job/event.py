###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
import errno
import json

from _flux._core import ffi
from flux.future import Future
from flux.job._wrapper import _RAW as RAW

# Names of events that may appear in the main eventlog (i.e. ``eventlog="eventlog"``)
# See Flux RFC 21 for documentation on each event.
MAIN_EVENTS = frozenset(
    {
        "submit",
        "depend",
        "priority",
        "flux-restart",
        "urgency",
        "alloc",
        "free",
        "start",
        "release",
        "finish",
        "clean",
        "debug",
        "exception",
    }
)


class EventLogEvent:
    """
    wrapper class for a single KVS EventLog entry
    """

    def __init__(self, event):
        """
        "Initialize from a string or dict eventlog event
        """
        if isinstance(event, str):
            event = json.loads(event)
        self._name = event["name"]
        self._timestamp = event["timestamp"]
        self._context = {}
        if "context" in event:
            self._context = event["context"]

    def __str__(self):
        return "{0.timestamp:<0.5f}: {0.name} {0.context}".format(self)

    @property
    def name(self):
        return self._name

    @property
    def timestamp(self):
        return self._timestamp

    @property
    def context(self):
        return self._context


class JobEventWatchFuture(Future):
    """
    A future returned from job.event_watch_async().
    Adds get_event() method to return an EventLogEntry event
    """

    def __del__(self):
        if self.needs_cancel is not False:
            self.cancel()
        try:
            super().__del__()
        except AttributeError:
            pass

    def __init__(self, future_handle):
        super().__init__(future_handle)
        self.needs_cancel = True

    def get_event(self, autoreset=True):
        """
        Return the next event from a JobEventWatchFuture, or None
        if the event stream has terminated.

        The future is auto-reset unless autoreset=False, so a subsequent
        call to get_event() will try to fetch the next event and thus
        may block.
        """
        result = ffi.new("char *[1]")
        try:
            #  Block until Future is ready:
            self.wait_for()
            RAW.event_watch_get(self.pimpl, result)
        except OSError as exc:
            if exc.errno == errno.ENODATA:
                self.needs_cancel = False
                return None
            # re-raise all other exceptions
            raise
        event = EventLogEvent(ffi.string(result[0]).decode("utf-8"))
        if autoreset is True:
            self.reset()
        return event

    def cancel(self, stop=False):
        """Cancel a streaming job.event_watch_async() future

        If stop=True, then deactivate the multi-response future so no
        further callbacks are called.
        """
        RAW.event_watch_cancel(self.pimpl)
        self.needs_cancel = False
        if stop:
            self.stop()


def event_watch_async(flux_handle, jobid, eventlog="eventlog"):
    """Asynchronously get eventlog updates for a job

    Asynchronously watch the events of a job eventlog.

    Returns a JobEventWatchFuture. Call .get_event() from the then
    callback to get the currently returned event from the Future object.

    .. seealso::

       :doc:`rfc:spec_21`
          Documentation for the events in the main eventlog

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID on which to watch events
    :param eventlog: eventlog path in job kvs directory (default: eventlog)
    :returns: a JobEventWatchFuture object
    :rtype: JobEventWatchFuture
    """

    future = RAW.event_watch(flux_handle, int(jobid), eventlog, 0)
    return JobEventWatchFuture(future)


def event_watch(flux_handle, jobid, eventlog="eventlog"):
    """Python generator to watch all events for a job

    Synchronously watch events a job eventlog via a simple generator.

    Example:
        >>> for event in job.event_watch(flux_handle, jobid):
        ...     # do something with event

    .. seealso::

       :doc:`rfc:spec_21`
          Documentation for the events in the main eventlog

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID on which to watch events
    :param eventlog: eventlog path in job kvs directory (default: eventlog)
    """
    watcher = event_watch_async(flux_handle, jobid, eventlog)
    event = watcher.get_event()
    while event is not None:
        yield event
        event = watcher.get_event()


class JobException(Exception):
    """Represents an 'exception' event occurring to a job.

    Instances expose a few public attributes.

    :var timestamp: the timestamp of the 'exception' event.
    :var type: A string identifying the type of job exception.
    :var note: Brief human-readable explanation of the exception.
    :var severity: the severity of the exception. Exceptions with a severity
        of 0 are fatal to the job; any other severity is non-fatal.
    """

    def __init__(self, event):
        self.timestamp = event.timestamp
        self.type = event.context["type"]
        self.note = event.context.get("note", "no explanation given")
        self.severity = event.context["severity"]
        super().__init__(self)

    def __str__(self):
        return f"job.exception: type={self.type}: {self.note}"


def event_wait(flux_handle, jobid, name, eventlog="eventlog", raiseJobException=True):
    """Wait for a job eventlog entry 'name'

    Wait synchronously for an eventlog entry named "name" and
    return the entry to caller, raises OSError with ENODATA if
    event never occurred

    .. seealso::

       :doc:`rfc:spec_21`
          Documentation for the events in the main eventlog

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID on which to wait for eventlog events
    :param name: The event name for which to wait
    :param eventlog: eventlog path in job kvs directory (default: eventlog)
    :param raiseJobException: if True, watch for job exception events and
      raise a JobException if one is seen before event 'name' (default=True)
    :returns: an EventLogEvent object, or raises OSError if eventlog
     ended before matching event was found
    :rtype: EventLogEvent
    """
    for event in event_watch(flux_handle, jobid, eventlog):
        if event.name == name:
            return event
        if (
            raiseJobException
            and event.name == "exception"
            and event.context["severity"] == 0
        ):
            raise JobException(event)
    raise OSError(errno.ENODATA, f"eventlog ended before event='{name}'")
