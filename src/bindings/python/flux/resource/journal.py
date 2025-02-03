###############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from flux.abc import JournalConsumerBase, JournalEventBase


class ResourceJournalEvent(JournalEventBase):
    """A container for an event from the resource journal

    Attributes:
        name (str): event name (See :rfc:`21` for possible event names)
        timestamp (float): event timestamp in seconds since the epoch
            with sub-millisecond precision.
        context (dict): context dictionary
            (See `RFC 21: Event Descriptions <https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_21.html#event-descriptions>`_.)
        context_string (str): context dict converted to comma separated
            key=value string.
        R (dict): For resource-define events, the instance R (See :rfc:`20`)
    """

    def __init__(self, event, R=None):
        super().__init__(event)
        self.R = R

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


class ResourceJournalConsumer(JournalConsumerBase):
    """Class for consuming the resource journal

    See :obj:`journal.JournalConsumerBase` for documentation of the
    journal consumer interface.
    """

    def __init__(self, flux_handle, since=0.0, include_sentinel=False):
        super().__init__(
            flux_handle,
            "resource.journal",
            full=True,
            since=since,
            include_sentinel=include_sentinel,
        )

    def process_response(self, resp):
        """Process a single response from the resource journal"""
        return {"R": resp.get("R")}

    def create_event(self, entry, R=None):
        """Create a ResourceJournalEvent from one event entry in a response"""
        return ResourceJournalEvent(entry, R)


# vi: ts=4 sw=4 expandtab
