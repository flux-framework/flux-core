###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import json


class EventLogEvent(dict):
    """
    wrapper class for a single KVS EventLog entry
    """

    def __init__(self, event):
        """
        "Initialize from a string or dict eventlog event
        """
        if isinstance(event, str):
            event = json.loads(event)
        super().__init__(event)
        if "context" not in self:
            self["context"] = {}

    def __str__(self):
        return "{0.timestamp:<0.5f}: {0.name} {0.context}".format(self)

    @property
    def name(self):
        return self["name"]

    @property
    def timestamp(self):
        return self["timestamp"]

    @property
    def context(self):
        return self["context"]

    @property
    def context_string(self):
        if not self.context:
            return ""
        return json.dumps(
            self.context, ensure_ascii=False, separators=(",", ":"), sort_keys=True
        )
