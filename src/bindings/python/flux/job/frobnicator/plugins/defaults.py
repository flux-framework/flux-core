##############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

"""Apply defaults to incoming jobspec based on broker config.

"""

from flux.job.frobnicator import FrobnicatorPlugin


class DefaultsConfig:
    """Convenience class for handling jobspec defaults configuration"""

    def __init__(self, config={}):
        self.defaults = {}
        self.queues = {}
        self.default_queue = None

        try:
            self.defaults = config["policy"]["jobspec"]["defaults"]["system"]
            self.default_queue = self.defaults["queue"]
        except KeyError:
            pass

        try:
            self.queues = config["queues"]
        except KeyError:
            pass

        self.validate_config()

    def validate_config(self):
        if self.queues and not isinstance(self.queues, dict):
            raise ValueError("queues must be a table")

        if self.default_queue and self.default_queue not in self.queues:
            raise ValueError(
                f"default queue '{self.default_queue}' must be in [queues]"
            )

        for queue in self.queues:
            self.queue_defaults(queue)

    def queue_defaults(self, name):
        if name and self.queues:
            if name not in self.queues:
                raise ValueError(f"invalid queue {name} specified")
            qconf = self.queues[name]
            try:
                return qconf["policy"]["jobspec"]["defaults"]["system"]
            except KeyError:
                return None
        return None

    def setattr_default(self, jobspec, attr, value):
        if attr == "duration" and jobspec.duration == 0:
            jobspec.duration = value
        elif attr not in jobspec.attributes["system"]:
            jobspec.setattr(f"system.{attr}", value)

    def apply_defaults(self, jobspec):
        """Apply general defaults then queue-specific defaults to jobspec"""

        for attr in self.defaults:
            self.setattr_default(jobspec, attr, self.defaults[attr])

        if jobspec.queue:
            if jobspec.queue not in self.queues:
                raise ValueError(f"Invalid queue '{jobspec.queue}' specified")
            queue_defaults = self.queue_defaults(jobspec.queue)
            if queue_defaults:
                for attr in queue_defaults:
                    self.setattr_default(jobspec, attr, queue_defaults[attr])
        elif self.queues:
            raise ValueError("no queue specified")


class Frobnicator(FrobnicatorPlugin):
    def __init__(self, parser):
        self.config = DefaultsConfig()
        super().__init__(parser)

    def configure(self, args, config):
        self.config = DefaultsConfig(config)

    def frob(self, jobspec, user, urgency, flags):
        self.config.apply_defaults(jobspec)
