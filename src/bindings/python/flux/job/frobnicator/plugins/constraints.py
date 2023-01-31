##############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

"""Apply constraints to incoming jobspec based on broker config.

"""

from flux.job.frobnicator import FrobnicatorPlugin


class QueueConfig:
    """Convenience class for handling jobspec queues configuration"""

    def __init__(self, config={}):
        self.queues = {}
        try:
            self.queues = config["queues"]
        except KeyError:
            pass

    def queue_properties(self, name):
        try:
            return self.queues[name]["requires"]
        except KeyError:
            return None

    def apply_constraints(self, jobspec):
        """Apply queue-specific constraints to jobspec"""

        if jobspec.queue:
            if jobspec.queue not in self.queues:
                raise ValueError(f"Invalid queue '{jobspec.queue}' specified")
            queue_properties = self.queue_properties(jobspec.queue)
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
                #  No existing "constraints" - set constraints to queue
                #  constraints
                jobspec.setattr("system.constraints", {"properties": queue_properties})


class Frobnicator(FrobnicatorPlugin):
    def __init__(self, parser):
        super().__init__(parser)

    def configure(self, args, config):
        self.config = QueueConfig(config)

    def frob(self, jobspec, user, urgency, flags):
        self.config.apply_constraints(jobspec)
