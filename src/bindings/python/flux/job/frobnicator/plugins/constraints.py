##############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

"""Apply queue constraints to incoming jobspec from the job-manager."""

from flux.job.frobnicator import FrobnicatorPlugin
from flux.queue import QueueConf


def apply_constraints(queue_conf, jobspec):
    """Apply a jobspec's queue's required properties as a constraint.

    'queue_conf' is a flux.queue.QueueConf: the job-manager's authoritative
    queue configuration, with RFC 33 virtual-queue inheritance already
    resolved.
    """
    if not jobspec.queue:
        return
    if jobspec.queue not in queue_conf:
        raise ValueError(f"Invalid queue '{jobspec.queue}' specified")
    # The queue's effective required properties. For a virtual queue (RFC
    # 33) this is the parent's, resolved by the job-manager - so applying it
    # schedules the job as part of the parent's job list.
    queue_properties = queue_conf.requires(jobspec.queue)
    if queue_properties is None:
        return

    # First try appending to existing constraints
    try:
        spec = jobspec.attributes["system"]["constraints"]["properties"]
        for prop in queue_properties:
            if prop not in spec:
                spec.append(prop)
        return
    except KeyError:
        #  No "properties" operator at top level, try combining
        #  existing constraints with logical AND
        pass
    try:
        jobspec.setattr(
            "system.constraints",
            {
                "and": [
                    jobspec.attributes["system"]["constraints"],
                    {"properties": queue_properties},
                ]
            },
        )
    except KeyError:
        #  No existing "constraints" - set constraints to queue constraints
        jobspec.setattr("system.constraints", {"properties": queue_properties})


class Frobnicator(FrobnicatorPlugin):
    def __init__(self, parser):
        super().__init__(parser)

    def configure(self, args, config):
        # queue_conf is injected by the framework as an attribute (see
        # FrobnicatorPlugin) before configure() is called.
        self.queue_conf = self.queue_conf or QueueConf({})

    def frob(self, jobspec, user, urgency, flags):
        apply_constraints(self.queue_conf, jobspec)
