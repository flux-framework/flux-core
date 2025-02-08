###############################################################
# Copyright 2024 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from flux.abc import JournalConsumerBase, JournalEventBase
from flux.job import JobID


class JournalEvent(JournalEventBase):
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


class JournalConsumer(JournalConsumerBase):
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

    SENTINEL_EVENT = SENTINEL_EVENT

    def __init__(self, flux_handle, full=True, since=0.0, include_sentinel=False):
        super().__init__(
            flux_handle,
            "job-manager.events-journal",
            full=full,
            since=since,
            include_sentinel=include_sentinel,
        )

    @property
    def request_payload(self):
        return {"full": self.full}

    def process_response(self, resp):
        """Process a single job manager journal response

        The response will contain the jobid and possibly jobspec and R,
        depending on the specific events in the payload. Return these in
        a dict so they are passed as keyword arguments to create_event.
        """
        return {
            "jobid": resp.get("id"),
            "jobspec": resp.get("jobspec"),
            "R": resp.get("R"),
        }

    def create_event(self, entry, jobid=-1, jobspec=None, R=None):
        """Create a single JournalEvent from one event entry"""
        return JournalEvent(jobid, entry, jobspec=jobspec, R=R)


# vi: ts=4 sw=4 expandtab
